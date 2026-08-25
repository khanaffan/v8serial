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
remain valid until `read()` returns. Decoded strings and binary payloads are
copied into the resulting native value tree.

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
  std::u16string string;
  std::vector<DecodedValue> array;
  std::vector<std::pair<std::u16string, DecodedValue>> object;
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
| `String` | `string` |
| `Array` | `array` |
| `Object` | `object` |
| `ArrayBuffer`, `Uint8Array` | `binary` |

Object properties preserve their serialized order. Integer property keys are
converted to their decimal UTF-16 string representation.

## Accepted encodings

The reader supports:

- undefined, null and booleans;
- signed and unsigned 32-bit numbers;
- doubles;
- UTF-8, Latin-1 and UTF-16 strings;
- dense arrays without holes or named properties;
- plain objects with string or integer keys;
- ordinary ArrayBuffers;
- native Uint8Array views, including valid offsets into a backing buffer;
- Node 22 host-object forms for Uint8Array and Buffer.

Node Buffer host objects decode as `DecodedType::Uint8Array`. The standalone
model intentionally does not carry Node-specific Buffer identity.

## Validation

The reader validates before accessing or allocating payloads:

- header and exact format version;
- varint width and termination;
- input bounds for every byte range;
- UTF-8 sequences and Unicode scalar values;
- even UTF-16 byte lengths;
- object property counts;
- dense-array element and terminal lengths;
- Uint8Array offsets, lengths and flags;
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

The reader explicitly rejects object references and cycles, sparse arrays,
holes, named array properties, other typed-array views, resizable/shared/
transferred buffers, BigInt, Date, boxed primitives, RegExp, Map, Set, Error,
WebAssembly values and shared heap objects.

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
