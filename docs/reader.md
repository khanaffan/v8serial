# C++ reader

`include/v8serial/reader.hpp` is a header-only, bounds-checked C++17 reader for
the supported subset of V8 serialization format version 15. It contains no V8,
Node.js or N-API calls and is designed to run in native worker threads.

## Basic use

```cpp
#include <cstdint>
#include <vector>

#include "v8serial/reader.hpp"

void process(const std::vector<uint8_t>& encoded) {
  v8serial::Reader reader(encoded);
  v8serial::DecodedValue root = reader.read();

  if (root.type != v8serial::DecodedType::Object) {
    throw std::runtime_error("expected an object");
  }

  for (const auto& [key, value] : root.object) {
    // Inspect the native value tree.
  }
}
```

## Input lifetime

```cpp
Reader(const uint8_t* data, size_t size);
explicit Reader(const std::vector<uint8_t>& data);
```

The reader borrows the input; it does not retain or mutate it. The bytes must
remain valid until `read()` or `readRows()` returns. `read()` copies decoded
strings and binary payloads into the resulting native value tree.

For native ArrayBuffer views, the reader validates the backing buffer and view
metadata before copying only the selected byte range. It does not allocate an
intermediate copy of the whole backing buffer. The result still owns its bytes
and does not alias the serialized input.

`read()` requires:

- a version header;
- exactly version 15;
- one complete supported root value;
- no trailing non-padding bytes.

## Native value model

`read()` returns `v8serial::DecodedValue`.

```cpp
struct DecodedValue {
  DecodedType type;
  bool boolean;
  int32_t int32;
  uint32_t uint32;
  double number;
  double date_milliseconds;
  std::u16string string;
  std::vector<DecodedValue> array;
  std::vector<std::pair<std::u16string, DecodedValue>> object;
  ArrayBufferViewType view_type;
  std::vector<uint8_t> binary;
};
```

Read the member selected by `type`:

| `DecodedType` | Member |
|---|---|
| `Undefined`, `Null` | No payload |
| `Boolean` | `boolean` |
| `Int32` | `int32` |
| `Uint32` | `uint32` |
| `Double` | `number` |
| `Date` | `date_milliseconds` |
| `String` | `string` |
| `Array` | `array` |
| `Object` | `object` |
| `ArrayBuffer`, `Uint8Array` | `binary` |
| `ArrayBufferView` | `view_type`, `binary` |

Object properties preserve their serialized order. Integer property keys are
converted to their decimal UTF-16 string representation.

## Streaming positional rows

`readRows()` consumes a root array of fixed-width positional rows without
constructing a `DecodedValue` tree:

```cpp
uint32_t rowCount = v8serial::Reader(data, size).readRows(
    5, [](uint32_t rowIndex, uint32_t columnIndex, uint32_t columnCount,
          const v8serial::ScalarValue& value) {
      bindValue(rowIndex, columnIndex, columnCount, value);
    });
```

The callback runs once per scalar. Rows must have a consistent width and at
least the requested minimum column count. Dense arrays and complete
sparse-array wire forms are accepted; holes, named properties, nested values,
and trailing bytes are rejected.

`ScalarValue::type` selects Boolean, signed or unsigned 32-bit integer, double,
null, undefined, Latin-1, UTF-8, or UTF-16 data. String bytes point directly
into the serialized input. They are not null terminated or guaranteed to be
aligned. Retain them only while also retaining the unchanged input.

Validation is incremental: callbacks for earlier cells can run before a later
malformed value is detected. Consumers that mutate a database or other external
state must use a transaction or equivalent rollback mechanism.

This API targets bulk data paths where materializing millions of native values
or reading every JavaScript array element through N-API would add unnecessary
allocation and boundary overhead.

## Accepted encodings

The reader supports:

- undefined, null and booleans;
- signed and unsigned 32-bit numbers;
- doubles;
- Dates;
- UTF-8, Latin-1 and UTF-16 strings;
- arrays without holes or named properties, in dense or sparse wire form;
- plain objects with string or integer keys;
- ordinary ArrayBuffers;
- native Node 22 typed-array and DataView forms, including valid offsets;
- Node 22 host-object forms for typed arrays, DataView and Buffer.

Node Buffer host objects decode as `DecodedType::Uint8Array`. The standalone
model intentionally does not carry Node-specific Buffer identity. Other binary
views use `DecodedType::ArrayBufferView` and identify their concrete type in
`view_type`.

## Validation

The reader validates before accessing or allocating payloads:

- header and exact format version;
- varint width and termination;
- input bounds for every byte range;
- UTF-8 sequences and Unicode scalar values;
- even UTF-16 byte lengths;
- object property counts;
- dense-array element and terminal lengths;
- binary-view types, element alignment, offsets, lengths and flags, before
  copying only the selected range;
- complete root-value consumption;
- nesting depth, limited to 512.

Malformed or unsupported input throws `v8serial::DecodeError`:

```cpp
try {
  v8serial::DecodedValue value = v8serial::Reader(data, size).read();
} catch (const v8serial::DecodeError& error) {
  std::cerr << error.what() << '\n';
  std::cerr << "offset: " << error.offset() << '\n';
}
```

`DecodeError::offset()` reports the byte position at which parsing failed.
Passing a null pointer with a nonzero size throws `std::invalid_argument`.

## Unsupported values

The reader explicitly rejects object references and cycles, array holes,
named array properties, resizable/shared/transferred buffers, BigInt, boxed
primitives, RegExp, Map, Set, Error, WebAssembly values and shared heap
objects. Float16Array is not available in the supported Node 22 runtime.

It fails on unsupported tags rather than returning a partially interpreted
value.

## Worker-thread use

```cpp
std::vector<uint8_t> owned_bytes = receiveWork();

std::thread worker([bytes = std::move(owned_bytes)] {
  v8serial::DecodedValue value = v8serial::Reader(bytes).read();
  processNativeValue(value);
});
```

Independent `Reader` instances are safe on separate threads. A single instance
must not be accessed concurrently. If the result eventually needs to become a
JavaScript value, construct it only after returning to the JavaScript thread.
The addon's `decodeAsync()` demonstrates this boundary.

## Resource limits

Bounds checking prevents out-of-range reads, but a valid large payload can
still allocate substantial memory. Applications processing untrusted data
should limit input size before constructing a reader.

See [V8 serialization wire format version 15](v8-format.md) for byte-level
details.
