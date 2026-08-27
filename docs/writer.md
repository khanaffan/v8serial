# C++ writer

`include/v8serial/writer.hpp` is a header-only C++17 writer for the supported
subset of V8 serialization format version 15. It has no V8, Node.js, N-API, or
platform runtime dependency.

## Basic use

```cpp
#include <cstdint>
#include <vector>

#include "v8serial/writer.hpp"

v8serial::Writer writer;
writer.beginObject();

writer.key(u"id");
writer.int32(42);

writer.key(u"name");
writer.string(u"beam");

writer.key(u"origin");
writer.beginObject();
writer.key(u"x");
writer.number(12.25);
writer.key(u"y");
writer.number(-8.5);
writer.endObject();

const uint8_t blob[] = {1, 2, 3};
writer.key(u"blob");
writer.uint8Array(blob, sizeof(blob));

writer.endObject();
std::vector<uint8_t> encoded = writer.take();
```

Node.js can consume `encoded` with:

```js
const value = require('node:v8').deserialize(Buffer.from(encoded));
```

## Construction and ownership

```cpp
explicit Writer(size_t initial_capacity = 256);
```

The writer owns its output in a `std::vector<uint8_t>`. The optional capacity
is a performance hint; it does not limit output size. Supplying an approximate
final size avoids vector reallocations for known schemas.

```cpp
std::vector<uint8_t> take();
```

`take()` moves the encoded buffer out. It throws if no root value was written
or a container remains open. After `take()` the writer's internal buffer is
empty and its state is unchanged; call `reset()` before writing another root
value.

### Reusing buffer capacity

```cpp
void reset();
const uint8_t* data() const;
size_t size() const;
```

`reset()` clears the writer's state (buffer contents, open containers, root
flag) and re-emits the version header, without releasing the buffer's
allocated capacity. This lets a single `Writer` instance be reused for many
messages in a row without a heap allocation per message, which is useful for
a producer thread that encodes many messages serially. `data()`/`size()`
provide read-only access to the bytes written so far, so a producer can copy
the current message out (e.g. into a queue or socket write for a consumer
thread) before calling `reset()` and encoding the next message — without
transferring buffer ownership the way `take()` does.

```cpp
v8serial::Writer writer(512);

for (const Record& record : records) {
  writer.reset();
  writeRecord(writer, record);

  // This operation must copy the bytes before it returns.
  copyToConsumerQueue(writer.data(), writer.size());
}
```

This reuse pattern is same-thread and serial: reset the writer, write one
complete message, copy its bytes, and only then reset again. Do not enqueue the
pointer returned by `data()` for later use: the next mutating writer call may
invalidate it or overwrite its contents. `data()` and `size()` expose the
current bytes but do not check that the root value and all containers are
complete.

Do not call `take()` in the reuse loop. It moves the vector out of the writer,
so its allocation is no longer available for the next message. `reset()`
retains the capacity of the writer's heap-backed vectors; `Writer` does not
currently provide a stack-buffer or custom-allocator mode.

A single `Writer` instance is still not safe for concurrent access from
multiple threads. See the [writer buffer reuse benchmark](performance.md#writer-buffer-reuse)
for measured fresh-versus-reused writer costs.

## Scalar methods

| Method | Encoded value |
|---|---|
| `undefined()` | JavaScript `undefined` |
| `null()` | JavaScript `null` |
| `boolean(bool)` | Boolean |
| `int32(int32_t)` | ZigZag-varint signed integer |
| `uint32(uint32_t)` | Unsigned varint integer |
| `number(double)` | IEEE-754 double |
| `date(double)` | Date milliseconds since the Unix epoch |
| `string(std::string_view)` | UTF-8 string |
| `string(std::u16string_view)` | Latin-1 or UTF-16 string |

Use `number()` for `-0`, NaN and infinities. The writer does not automatically
choose between `int32()` and `number()` because the caller already knows the
native value type.

The UTF-16 overload emits the compact one-byte representation when every code
unit is at most `0xff`; otherwise it emits an aligned two-byte string. The
UTF-8 overload emits V8's valid `kUtf8String` form.

## Objects

```cpp
writer.beginObject();
writer.key(u"enabled");
writer.boolean(true);
writer.endObject();
```

Every `key()` must be followed by exactly one value. Keys are UTF-16 strings.
The writer tracks and emits the terminal property count.

Invalid sequences such as a value without a key, two consecutive keys, or
closing an array with `endObject()` throw `std::logic_error`.

## Dense arrays

```cpp
writer.beginArray(3);
writer.null();
writer.int32(7);
writer.string(u"value");
writer.endArray();
```

The dense length is required before writing elements. Exactly that many values
must be written. Sparse arrays, holes and named array properties are not
supported.

## Binary values

```cpp
writer.arrayBuffer(data, size);
writer.uint8Array(data, size);
writer.arrayBufferView(v8serial::ArrayBufferViewType::Int16Array, data, size);
```

Both methods copy `size` bytes into the output stream, so `data` only needs to
remain valid for the duration of the call. A null pointer is accepted only
when `size == 0`.

`uint8Array()` and `arrayBufferView()` emit an ordinary ArrayBuffer immediately
followed by a view with offset zero, matching V8's native view grammar. They do
not emit Node's host-object representation. Typed-array byte lengths must be
multiples of their element sizes.

## Errors and limits

- More than one root value: `std::logic_error`
- Unbalanced or mismatched containers: `std::logic_error`
- Incorrect dense-array element count: `std::logic_error`
- Null data for non-empty binary input: `std::invalid_argument`
- Value or payload larger than `UINT32_MAX`: `std::length_error`

The low-level writer does not impose a nesting limit. Code accepting untrusted
or recursive input should enforce one before calling it; the included Node
addon uses a limit of 512 containers.

## Threading

Separate `Writer` instances can run concurrently on independent threads. A
single instance must not be accessed concurrently. The writer has no global
state and performs no V8 or N-API calls.

## Format scope

The writer supports undefined, null, booleans, signed and unsigned 32-bit
integers, double, Date, strings, dense arrays, plain string-keyed objects,
ArrayBuffer, Node 22 typed arrays and DataView. It does not preserve shared
identity or cycles and does not support other V8 tags.

See [V8 serialization wire format version 15](v8-format.md) for byte-level
details.
