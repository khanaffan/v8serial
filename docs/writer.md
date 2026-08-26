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
or a container remains open. A `Writer` is intended for one root value and
should not be reused after `take()`.

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
