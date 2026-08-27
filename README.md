# v8serial

`v8serial` reads and writes V8 serialization-format buffers in C++ without
linking V8. Node.js can reconstruct writer output with `v8.deserialize()`, and
native worker threads can parse supported `v8.serialize()` output without
accessing V8 or N-API. This avoids JSON's base64 conversion and size inflation
for binary data.

The project contains:

- [`include/v8serial/writer.hpp`](include/v8serial/writer.hpp): standalone
  C++17 writer ([writer guide](docs/writer.md)).
- [`include/v8serial/reader.hpp`](include/v8serial/reader.hpp): standalone,
  bounds-checked C++17 reader ([reader guide](docs/reader.md)).
- `encodeSync(value)`: test and comparison binding.
- `encodeAsync(value, callback)`: copies the JS value to native data, serializes
  it on a worker thread, and returns a `Buffer` through a
  `ThreadSafeFunction`. Large results transfer their native allocation without
  another copy; small results use Node-owned storage.
- `decodeSync(input)`: test binding for the standalone reader. `input` may be
  an `ArrayBuffer`, any typed array (including `Buffer`), or `DataView`.
- `decodeAsync(input, callback)`: copies the input view's byte range on the
  main thread, parses only native data on a worker thread, and constructs the
  result on the main thread.

```js
const v8 = require('node:v8');
const { encodeAsync } = require('v8serial');

encodeAsync({ id: 42, blob: new Uint8Array([1, 2, 3]) }, (error, buffer) => {
  if (error) throw error;
  const value = v8.deserialize(buffer);
});
```

The intended production path is to use `v8serial::Writer` directly where a
native worker produces its result. The addon copies JS input before dispatch
because N-API values cannot be accessed from a worker thread.

## C++ API

```cpp
v8serial::Writer writer;
writer.beginObject();
writer.key(u"id");
writer.int32(42);
writer.key(u"blob");
writer.uint8Array(data, size);
writer.endObject();
std::vector<uint8_t> encoded = writer.take();
```

For repeated messages on one producer thread, keep the writer's allocated
capacity instead of moving it out:

```cpp
v8serial::Writer writer(512);
for (const Record& record : records) {
  writer.reset();
  writeRecord(writer, record);
  copyToConsumerQueue(writer.data(), writer.size());
}
```

The handoff must copy the bytes before the writer is changed again. Calling
`take()` transfers the vector and therefore does not preserve its allocation
for reuse. See the [writer ownership guide](docs/writer.md#reusing-buffer-capacity)
and the [fresh-versus-reused benchmark](docs/performance.md#writer-buffer-reuse).

The reader returns a native value tree:

```cpp
v8serial::Reader reader(encoded.data(), encoded.size());
v8serial::DecodedValue value = reader.read();
```

`Reader` owns no global state and uses no V8 or N-API APIs, so independent
instances can run concurrently on worker threads. The input bytes must remain
alive until `read()` returns. It requires a version-15 header, consumes exactly
one value, validates lengths and terminal counts, and throws
`v8serial::DecodeError` for malformed or unsupported input.

Long Latin-1 strings use NEON on AArch64 and SSE2 on x86-64 for UTF-16
classification and conversion. Other targets use the same scalar code path
without requiring architecture-specific compiler flags.

Arrays require their dense length up front:

```cpp
writer.beginArray(2);
writer.null();
writer.boolean(true);
writer.endArray();
```

## Supported V8 features

This is not a complete implementation of every V8 serialization tag. It
implements the core subset needed for ordinary data objects and binary blobs.

| Value or feature | Writer | Reader | Notes |
|---|:---:|:---:|---|
| `undefined`, `null`, Boolean | Yes | Yes | |
| Signed int32 | Yes | Yes | ZigZag varint |
| Unsigned uint32 | Yes | Yes | Explicit C++ writer method |
| Double, NaN, infinities, `-0` | Yes | Yes | IEEE-754 binary64 |
| Latin-1 string | Yes | Yes | |
| UTF-8 string | Yes | Yes | Writer expects valid UTF-8 |
| UTF-16 string | Yes | Yes | Includes V8 alignment padding |
| Plain object | Yes | Yes | String keys; reader also accepts integer keys |
| Dense array | Yes | Yes | No holes or named properties |
| Sparse array wire form | No | Yes | Every index must be present in order |
| Ordinary `ArrayBuffer` | Yes | Yes | |
| Native typed arrays and `DataView` | Yes | Yes | Writer uses offset zero and flags zero |
| Node host-object typed arrays/DataView | No | Yes | Produced by Node's `v8.serialize()` |
| Node host-object `Buffer` | No | Yes | Decodes as `DecodedType::Uint8Array` |
| Shared references | No | No | Identity is not preserved |
| Cyclic objects | No | No | Rejected rather than emitting references |
| Sparse arrays and holes | No | No | Sparse wire form is accepted only for complete arrays |
| Named array properties | No | No | |
| BigInt | No | No | Includes boxed BigInt |
| Date | Yes | Yes | Milliseconds since epoch, including invalid Date |
| Boxed Boolean, Number, String | No | No | |
| RegExp | No | No | |
| Map and Set | No | No | |
| Error objects | No | No | |
| Resizable or transferred ArrayBuffer | No | No | |
| `SharedArrayBuffer` | No | No | Requires delegate-managed IDs |
| V8 shared heap objects | No | No | Version-15 shared-value tag |
| WebAssembly module or memory | No | No | Requires a V8 delegate |
| Custom host objects | No | Limited | Node binary-view forms only |
| Legacy formats (versions 0-14) | No | No | Exact version 15 is required |

Unsupported writer inputs throw before producing a buffer. Unsupported reader
tags throw `v8serial::DecodeError`; they are never silently interpreted as a
different value. Values deeper than 512 nested containers are rejected instead
of risking a native stack overflow.

`Blob` is a Node host object and cannot be reconstructed by a plain
`v8.deserialize()` call. Serialize its bytes as `Uint8Array`, then construct a
new `Blob` in JavaScript if needed.

## Compatibility

The wire format is V8-specific. This implementation emits format version 15
and is intentionally limited to Node.js 22. The JavaScript entry point compares
the addon's format version with the running V8 serializer and fails during
loading if they differ. See the
[source-derived format reference](docs/v8-format.md) for the complete tag,
writer, parser, and version-compatibility details.

## Build, test, and benchmark

```sh
npm install
npm test
npm run bench
```

See the generated [performance report](docs/performance.md) for comparisons
between the direct C++ headers, addon bridge, Node's V8 codec, JSON byte arrays,
and JSON base64 across scalar, geometry, collection, and blob payloads.

The test suite includes source-derived golden vectors, differential checks
against Node's V8 implementation, deterministic generated value trees,
malformed and truncated input, async concurrency and worker teardown, plus a
standalone native C++ test executable.
