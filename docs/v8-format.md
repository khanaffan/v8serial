# V8 serialization wire format version 15

This document describes the serialization format read by Node.js 22.20.0's
`v8.deserialize()` and written by the underlying V8 `ValueSerializer`.

It is an implementation-derived reference, not a public V8 specification. V8
promises backward compatibility in its source, but it does not publish this
format as a stable external protocol. Persisted data should therefore record
its format version and treat deserialization failure as an expected error.

## Source baseline

This document is based on Node.js tag
[`v22.20.0`](https://github.com/nodejs/node/tree/v22.20.0), which vendors V8
serialization format version 15:

- [`v8-value-serializer-version.h`](https://github.com/nodejs/node/blob/v22.20.0/deps/v8/include/v8-value-serializer-version.h)
- [`value-serializer.cc`](https://github.com/nodejs/node/blob/v22.20.0/deps/v8/src/objects/value-serializer.cc)
- [`value-serializer.h`](https://github.com/nodejs/node/blob/v22.20.0/deps/v8/src/objects/value-serializer.h)
- [`node_serdes.cc`](https://github.com/nodejs/node/blob/v22.20.0/src/node_serdes.cc)
- [`lib/v8.js`](https://github.com/nodejs/node/blob/v22.20.0/lib/v8.js)

The words **wire rule** below describe bytes that a compatible version-15
writer or reader must handle. **V8 behavior** describes current implementation
choices that valid alternate writers do not necessarily need to reproduce.

## Message framing

A version-15 message starts with:

```text
ff 0f
│  └─ unsigned varint: format version 15
└──── kVersion
```

General form:

```text
kVersion version:varuint32 rootValue
```

The reader accepts a stream without `kVersion` as legacy version 0. It rejects
a declared version newer than its own `kLatestVersion`.

The stream contains one root value. V8's low-level deserializer may not require
that every trailing byte is consumed, so an application that uses this as an
external protocol should enforce full-buffer consumption itself.

## Primitive encodings

### Unsigned varint

Integers use base-128 varints with the least-significant seven-bit group first.
Bit 7 indicates that another byte follows.

```text
encode(value):
  repeat:
    next = value & 0x7f
    value >>= 7
    if value != 0: next |= 0x80
    emit next
  until value == 0
```

Examples:

| Value | Bytes |
|---:|---|
| 0 | `00` |
| 1 | `01` |
| 127 | `7f` |
| 128 | `80 01` |
| 300 | `ac 02` |

Lengths and IDs in version 15 are normally bounded to unsigned 32 bits.

### Signed int32

`kInt32` values first use ZigZag conversion, then unsigned varint:

```text
encoded = (uint32(value) << 1) ^ uint32(value >> 31)
decoded = (encoded >> 1) ^ -(encoded & 1)
```

| Value | Encoded integer |
|---:|---:|
| 0 | 0 |
| -1 | 1 |
| 1 | 2 |
| -2 | 3 |
| `INT32_MIN` | `UINT32_MAX` |

### Double

A double is eight raw bytes containing an IEEE-754 binary64 value. V8 writes
and reads these bytes in host byte order. It normalizes deserialized NaNs to a
quiet NaN.

This makes doubles non-portable between little-endian and big-endian systems.
`v8serial` currently emits little-endian bytes and therefore targets
little-endian Node.js platforms.

### Padding

`kPadding` is byte `00`. A reader skips padding while looking for the next tag.

Before a two-byte string, the writer emits one padding byte when necessary so
the UTF-16 payload begins at an even stream offset:

```text
if (currentOffset + 1 + varintSize(byteLength)) is odd:
  emit kPadding
emit kTwoByteString
emit byteLength
emit UTF-16 payload
```

## Serialization tags

| Tag | Byte | Payload or purpose |
|---|---:|---|
| `kVersion` | `ff` | Format version varuint32 |
| `kPadding` | `00` | Ignored while reading tags |
| `kVerifyObjectCount` | `3f` `?` | Legacy count varuint32; reader ignores it |
| `kTheHole` | `2d` `-` | Missing dense-array element |
| `kUndefined` | `5f` `_` | `undefined` |
| `kNull` | `30` `0` | `null` |
| `kTrue` | `54` `T` | `true` |
| `kFalse` | `46` `F` | `false` |
| `kInt32` | `49` `I` | ZigZag varint |
| `kUint32` | `55` `U` | Unsigned varint |
| `kDouble` | `4e` `N` | Eight host-endian bytes |
| `kBigInt` | `5a` `Z` | BigInt bitfield and digit bytes |
| `kUtf8String` | `53` `S` | Byte length and UTF-8 bytes |
| `kOneByteString` | `22` `"` | Byte length and Latin-1 bytes |
| `kTwoByteString` | `63` `c` | Byte length and UTF-16 bytes |
| `kObjectReference` | `5e` `^` | Object ID varuint32 |
| `kBeginJSObject` | `6f` `o` | Plain-object start |
| `kEndJSObject` | `7b` `{` | Property count varuint32 |
| `kBeginSparseJSArray` | `61` `a` | Initial array length |
| `kEndSparseJSArray` | `40` `@` | Property count and repeated length |
| `kBeginDenseJSArray` | `41` `A` | Initial array length |
| `kEndDenseJSArray` | `24` `$` | Named-property count and repeated length |
| `kDate` | `44` `D` | Milliseconds as a double |
| `kTrueObject` | `79` `y` | Boxed `true` |
| `kFalseObject` | `78` `x` | Boxed `false` |
| `kNumberObject` | `6e` `n` | Boxed double |
| `kBigIntObject` | `7a` `z` | Boxed BigInt payload |
| `kStringObject` | `73` `s` | Boxed serialized string |
| `kRegExp` | `52` `R` | Source string and flags varuint32 |
| `kBeginJSMap` | `3b` `;` | Map start |
| `kEndJSMap` | `3a` `:` | Twice the entry count |
| `kBeginJSSet` | `27` `'` | Set start |
| `kEndJSSet` | `2c` `,` | Entry count |
| `kArrayBuffer` | `42` `B` | Byte length and raw bytes |
| `kResizableArrayBuffer` | `7e` `~` | Length, maximum length, raw bytes |
| `kArrayBufferTransfer` | `74` `t` | Transfer ID |
| `kArrayBufferView` | `56` `V` | View type, offset, length, flags |
| `kSharedArrayBuffer` | `75` `u` | Delegate-provided shared-buffer ID |
| `kSharedObject` | `70` `p` | Shared-value ID; version 15 or newer |
| `kWasmModuleTransfer` | `77` `w` | Delegate-provided transfer ID |
| `kHostObject` | `5c` `\` | Delegate-defined payload |
| `kWasmMemoryTransfer` | `6d` `m` | Limits, memory mode and shared buffer |
| `kError` | `72` `r` | Error sub-tag sequence |

Several additional byte values are reserved for legacy Chromium tags and must
not be reused without a format-version change.

## Strings

Three string representations are accepted:

```text
kUtf8String     byteLength:varuint32 bytes[byteLength]
kOneByteString  byteLength:varuint32 latin1[byteLength]
kTwoByteString  byteLength:varuint32 utf16[byteLength]
```

Rules:

- The two-byte length is a byte count and must be even.
- Two-byte code units use host byte order in V8. On ordinary Node platforms
  this is UTF-16LE.
- Current V8 normally writes one-byte strings for Latin-1 content and two-byte
  strings otherwise.
- Current V8 does not normally emit `kUtf8String` for JavaScript strings, but
  the version-15 reader accepts it.
- Invalid lengths, truncated bytes or failed UTF-8 conversion make parsing
  fail.

`v8serial::Writer::string(std::string_view)` uses `kUtf8String`, while its
UTF-16 overload selects one-byte or two-byte representation. Both forms are
valid, although the UTF-8 overload is not generally byte-identical to V8's own
writer.

## Object identity and references

Compound values receive monotonically increasing IDs starting at zero. A
reader registers an object before reading its children. A repeated reference
is encoded as:

```text
kObjectReference objectId:varuint32
```

The ID must already exist when parsed. This mechanism preserves shared
identity and cycles.

Primitive values, strings and BigInts do not receive object IDs. Objects,
arrays, Maps, Sets, Dates, RegExps, boxed primitives, Errors, ArrayBuffers and
views do.

The current `v8serial` subset intentionally does not emit references. It
rejects cycles and serializes repeated non-cyclic objects independently, so
their identity is not preserved.

## Per-value writer grammar

`Value` means another complete tagged value.

### Scalar values

```text
undefined := kUndefined
null      := kNull
true      := kTrue
false     := kFalse
int32     := kInt32 zigzag-varuint32
uint32    := kUint32 varuint32
number    := kDouble raw-double
string    := one of the string encodings above
```

V8 writes Smi numbers as `kInt32` and heap numbers as `kDouble`. Therefore
`-0`, infinities and NaN use `kDouble`. The exact Smi range is a V8 platform
detail, not a semantic requirement for another valid writer.

### BigInt

```text
kBigInt bitfield:varuint32 digits[byteCountFromBitfield]
```

The bitfield records sign and digit-byte count using V8's
`BigInt::GetBitfieldForSerialization()` layout. Digits use V8's internal
serialization representation. A portable third-party writer should derive
this layout from the exact V8 source version instead of assuming it remains
unchanged.

### Plain object

```text
kBeginJSObject
  (key:Value value:Value) * propertyCount
kEndJSObject
propertyCount:varuint32
```

The reader counts decoded key/value pairs and requires that count to equal the
terminal count. Keys must be valid JavaScript property keys. V8's writer
serializes own enumerable properties and may execute accessors while obtaining
values.

### Dense array

```text
kBeginDenseJSArray
length:varuint32
  (Value | kTheHole) * length
  (key:Value value:Value) * namedPropertyCount
kEndDenseJSArray
namedPropertyCount:varuint32
lengthAgain:varuint32
```

The parser requires `lengthAgain == length` and verifies the number of named
properties. Before version 11, `kUndefined` in this context was interpreted as
a hole; version 11 separated holes from explicit `undefined`.

### Sparse array

```text
kBeginSparseJSArray
length:varuint32
  (key:Value value:Value) * propertyCount
kEndSparseJSArray
propertyCount:varuint32
lengthAgain:varuint32
```

Numeric indices are ordinary properties in this form. The parser verifies both
terminal values.

Whether V8 chooses dense or sparse form is an internal elements-kind decision,
not solely a function of visible array density.

### Date

```text
kDate milliseconds:raw-double
```

The result is registered in the object-ID table.

### Boxed primitives

```text
kTrueObject
kFalseObject
kNumberObject raw-double
kBigIntObject bigint-payload
kStringObject serialized-string
```

### RegExp

```text
kRegExp source:serialized-string flags:varuint32
```

The parser rejects unknown or disabled RegExp flags.

### Map

```text
kBeginJSMap
  (key:Value value:Value) * entryCount
kEndJSMap
serializedValueCount:varuint32
```

`serializedValueCount` must equal `entryCount * 2`.

### Set

```text
kBeginJSSet
  value:Value * entryCount
kEndJSSet
entryCount:varuint32
```

### ArrayBuffer

Ordinary:

```text
kArrayBuffer
byteLength:varuint32
bytes[byteLength]
```

Resizable:

```text
kResizableArrayBuffer
byteLength:varuint32
maxByteLength:varuint32
bytes[byteLength]
```

Transferred:

```text
kArrayBufferTransfer transferId:varuint32
```

The receiving `ValueDeserializer` must have the corresponding buffer
registered with `TransferArrayBuffer()` before reading.

Shared:

```text
kSharedArrayBuffer sharedBufferId:varuint32
```

Shared-buffer IDs are application-defined and require serializer/deserializer
delegates. Detached buffers and buffers larger than the format's unsigned
32-bit length limit cannot be serialized normally.

### ArrayBufferView

A view is encoded immediately after its backing buffer value:

```text
backingBuffer:Value
kArrayBufferView
viewType:varuint32
byteOffset:varuint32
byteLength:varuint32
flags:varuint32
```

The flags field exists in version 14 and later:

| Bit | Meaning |
|---:|---|
| 0 | Length-tracking view |
| 1 | Backed by a non-shared resizable ArrayBuffer |

View type values:

| Byte | View |
|---:|---|
| `62` `b` | `Int8Array` |
| `42` `B` | `Uint8Array` |
| `43` `C` | `Uint8ClampedArray` |
| `77` `w` | `Int16Array` |
| `57` `W` | `Uint16Array` |
| `64` `d` | `Int32Array` |
| `44` `D` | `Uint32Array` |
| `68` `h` | `Float16Array` |
| `66` `f` | `Float32Array` |
| `46` `F` | `Float64Array` |
| `71` `q` | `BigInt64Array` |
| `51` `Q` | `BigUint64Array` |
| `3f` `?` | `DataView` |

Parser validation includes:

- offset and length must be multiples of the element size;
- offset plus length must fit inside the backing buffer;
- resizable and length-tracking flags must agree with the buffer type;
- non-shared resizable buffers require the RAB-backed flag;
- Float16Array requires V8's corresponding feature flag;
- unknown view types fail.

The reader first creates or resolves the backing buffer, then peeks for
`kArrayBufferView` and replaces the returned buffer value with the constructed
view.

### Error

An Error starts with `kError`, followed by sub-tags:

| Sub-tag | Byte | Payload |
|---|---:|---|
| EvalError prototype | `45` `E` | none |
| RangeError prototype | `52` `R` | none |
| ReferenceError prototype | `46` `F` | none |
| SyntaxError prototype | `53` `S` | none |
| TypeError prototype | `54` `T` | none |
| URIError prototype | `55` `U` | none |
| Message | `6d` `m` | serialized string |
| Cause | `63` `c` | any value |
| Stack | `73` `s` | serialized string |
| End | `2e` `.` | terminator |

The prototype marker, if present, comes first. V8 writes message, stack and
cause according to its implementation's expected sequence. An unknown or
misordered sub-tag fails parsing.

### Shared objects

Version 15 adds:

```text
kSharedObject sharedValueId:varuint32
```

Resolving the ID requires the V8 shared-value delegate/conveyor.

### WebAssembly

Module:

```text
kWasmModuleTransfer transferId:varuint32
```

Memory:

```text
kWasmMemoryTransfer
maximumPages:zigzag-varint32
memory64:byte
sharedArrayBuffer:Value
```

These forms require delegate support. `memory64` must be zero or one.

### Host objects

```text
kHostObject delegateDefinedPayload
```

V8 does not define the payload. The writer delegate and reader delegate must
agree on its framing and semantics.

## Reader algorithm

A version-15 parser can be modeled as follows:

```text
readHeader():
  if peekTag() == kVersion:
    consume tag
    version = readVaruint32()
  else:
    version = 0
  reject version > 15

readValue():
  skip kPadding
  tag = readByte()
  switch tag:
    decode primitive, string or compound value
    verify all lengths, terminal counts and references
    register compound objects before reading children
  if decoded value is an ArrayBuffer and next tag is kArrayBufferView:
    decode and return its view
  return value
```

Important parser rules:

- A read beyond the end of the input fails.
- Unknown tags fail for version 13 and newer.
- Before version 13, unknown tags are offered to the host-object delegate.
- `kObjectReference` fails if its ID is not registered.
- Odd two-byte-string lengths fail.
- Compound terminal counts and repeated lengths must match observed values.
- ArrayBuffer and view lengths must fit their input and backing storage.
- RegExp flags and view flags are validated.
- Delegate-dependent tags fail when the required delegate operation fails.
- V8 ultimately reports an unsuccessful value read as a data-clone
  deserialization error unless another JavaScript exception is already pending.

### Version-13 recovery

Some V8 version-13 data was emitted with the later ArrayBufferView flag field.
When normal parsing of version-13 input fails without another pending
exception, V8 resets the reader and retries in a compatibility mode that reads
view flags. Version 14 made those flags part of the regular format.

## Writer algorithm

V8's high-level writer follows this shape:

```text
writeHeader()
writeValue(root):
  if primitive or string:
    emit its scalar representation
    return

  if object was already assigned an ID:
    emit kObjectReference and ID
    return

  assign the next object ID
  dispatch by object type
  emit children recursively
  emit and verify terminal metadata where applicable
```

The object is entered into the identity map before its children are written,
which permits repeated references and cycles. Serialization can still fail for
unsupported values, detached buffers, invalid/out-of-bounds views, failed
delegate operations, property access exceptions or excessive recursion.

## Node.js wrapper behavior

Node's `v8.serialize()` does not expose raw V8 defaults unchanged.
`DefaultSerializer` enables `treatArrayBufferViewsAsHostObjects`.

Consequently, Node writes Buffer and typed-array values as `kHostObject`
payloads containing:

```text
nodeTypeIndex:varuint32
byteLength:varuint32
rawViewBytes[byteLength]
```

Node's host type indexes are:

| Index | Type |
|---:|---|
| 0 | `Int8Array` |
| 1 | `Uint8Array` |
| 2 | `Uint8ClampedArray` |
| 3 | `Int16Array` |
| 4 | `Uint16Array` |
| 5 | `Int32Array` |
| 6 | `Uint32Array` |
| 7 | `Float32Array` |
| 8 | `Float64Array` |
| 9 | `DataView` |
| 10 | Node `Buffer` |
| 11 | `BigInt64Array` |
| 12 | `BigUint64Array` |
| 13 | `Float16Array` |

The matching `DefaultDeserializer` reconstructs the selected view. It copies
bytes when required to satisfy the view type's alignment.

This host representation prevents Node Buffers backed by a larger pooled
ArrayBuffer from accidentally serializing unrelated bytes. It also means:

- Node's `v8.serialize(new Uint8Array(...))` is not byte-identical to raw V8's
  native ArrayBuffer-plus-view representation.
- Plain `v8.deserialize()` accepts both Node host-object views and valid native
  ArrayBufferView encoding because it supplies Node's host delegate while V8
  retains its built-in view parser.
- A non-Node consumer cannot interpret Node host-object payloads without
  implementing Node's host-object convention.

## Compatibility history

| Version | Relevant change |
|---:|---|
| 0 | Legacy headerless format |
| 9 | Baseline imported from Blink |
| 10 | One-byte Latin-1 string encoding |
| 11 | Distinct `kTheHole` versus `undefined` |
| 12 | Tagged string parsing in compound contexts |
| 13 | Explicit `kHostObject`; unknown modern tags become errors |
| 14 | ArrayBufferView flags field |
| 15 | Shared V8 heap object tag |

A version-15 reader is intended to retain support for valid older messages.
A version-15 writer must not assume that an older reader understands tags or
fields introduced after that reader's version.

## Resource and trust considerations

Treat serialized data as untrusted binary input:

- Validate the version before allocating.
- Bound total input size, nesting depth, collection sizes and output
  allocations at the application boundary.
- Dense-array lengths are checked by V8, but valid large input can still cause
  large allocations.
- ArrayBuffer lengths can request allocations proportional to the supplied
  input.
- Recursive compound values can consume significant stack space.
- Host-object payloads are trusted according to the supplied delegate.
- Deserialization constructs objects and may retain large backing stores.
- This format provides no authentication, integrity protection or encryption.

`v8serial` limits input nesting to 512 containers and restricts itself to a
small, explicitly documented subset. Those are application safeguards, not
requirements of the V8 wire format.

## `v8serial` conformance summary

The implementation in this repository emits valid version-15 forms for:

- undefined, null and booleans;
- signed int32 and double;
- UTF-8, Latin-1 and UTF-16 strings;
- plain objects;
- dense arrays without named properties;
- ordinary ArrayBuffers;
- ordinary Uint8Array views with offset zero and flags zero.

Its standalone C++ reader accepts those forms, unsigned-int32 values, and
Node's host-object encoding for Uint8Array and Buffer. It validates bounds,
container terminal counts, view ranges, version and complete input
consumption, and can operate on a native worker thread without V8 or N-API.

It does not currently implement references/cycles, sparse arrays, other view
types, BigInt, Date, boxed primitives, RegExp, Map, Set, Error, resizable,
shared or transferred buffers, WebAssembly values, shared heap objects or host
objects.

Conformance means the supported output is accepted by the Node 22.20.0
version-15 parser. It does not mean every buffer is byte-identical to
`v8.serialize()`, because Node uses host-object encoding for views and V8's
writer makes representation-dependent choices for strings, numbers, property
order and dense versus sparse arrays.
