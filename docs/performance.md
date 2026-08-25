# Performance

Generated 2026-08-25T14:18:32.382Z on Apple M4 Max (arm64), Node v22.20.0, V8 12.4.254.21-node.33.

![Performance comparison](performance.svg)

## Methodology

- Seven timed rounds per operation; tables report the median nanoseconds per operation.
- Every operation is warmed up first and its result is consumed.
- Encode and decode are measured separately; round trip is the sum of their medians.
- `C++ headers (SIMD)` calls `Writer` and `Reader` directly with native values and enables the architecture-specific string paths.
- `C++ headers (scalar)` builds the same benchmark with `V8SERIAL_DISABLE_SIMD=1` for a like-for-like baseline.
- `v8serial addon` includes generic JavaScript object traversal and N-API boundary cost.
- `Node v8` is Node.js `v8.serialize()` and `v8.deserialize()`.
- `JSON text` applies only to payloads without binary values.
- `JSON byte array` preserves Uint8Array as JSON decimal byte arrays.
- `JSON base64` preserves Uint8Array using base64 text and includes conversion in both timings.
- Results are machine-specific; regenerate with `npm run bench`.
- The C++ reader returns owning strings and byte vectors. Node may reconstruct a host-object typed array as a view into the serialized input, so its large-blob decode has different ownership semantics.

## Highlights

- JSON text is fastest for scalar and small plain JavaScript values because V8 has highly optimized built-in JSON paths.
- SIMD makes the 4 KiB Latin-1 native round trip 5.6x faster than the scalar path.
- For geometry with a 64-byte blob, direct C++ headers are 5.3x faster than JSON base64. The generic addon bridge is 1.1x the JSON-base64 latency because JavaScript property traversal dominates this small payload.
- For a 1 MiB blob, direct C++ headers are 78.0x faster and the addon bridge is 18.6x faster than JSON base64, while avoiding base64's wire-size expansion.
- Node V8 is exceptionally fast for large typed arrays because its host-object deserializer may return a view into the serialized input; the standalone reader instead returns owning native bytes.

## Results

### Scalar int32

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 30 ns | 10 ns | 40 ns | 25,251,824 | 4 B |
| C++ headers (scalar) | 29 ns | 10 ns | 40 ns | 25,267,456 | 4 B |
| v8serial addon | 416 ns | 71 ns | 487 ns | 2,052,839 | 4 B |
| Node v8 | 276 ns | 209 ns | 486 ns | 2,059,356 | 4 B |
| JSON text | 78 ns | 84 ns | 162 ns | 6,178,460 | 2 B |

### Point3d

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 63 ns | 149 ns | 212 ns | 4,714,406 | 41 B |
| C++ headers (scalar) | 63 ns | 149 ns | 212 ns | 4,719,474 | 41 B |
| v8serial addon | 932 ns | 492 ns | 1.42 us | 702,202 | 41 B |
| Node v8 | 467 ns | 438 ns | 905 ns | 1,104,874 | 41 B |
| JSON text | 237 ns | 247 ns | 485 ns | 2,063,497 | 31 B |

### Nested geometry

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 377 ns | 1.38 us | 1.75 us | 570,596 | 359 B |
| C++ headers (scalar) | 396 ns | 1.40 us | 1.79 us | 557,274 | 359 B |
| v8serial addon | 5.13 us | 4.14 us | 9.28 us | 107,799 | 359 B |
| Node v8 | 1.01 us | 1.62 us | 2.63 us | 379,605 | 366 B |
| JSON text | 946 ns | 1.21 us | 2.16 us | 463,862 | 353 B |

### Geometry + 64 B

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 369 ns | 1.36 us | 1.73 us | 576,759 | 436 B |
| C++ headers (scalar) | 412 ns | 1.47 us | 1.88 us | 531,996 | 436 B |
| v8serial addon | 5.27 us | 4.47 us | 9.74 us | 102,668 | 436 B |
| Node v8 | 1.22 us | 1.75 us | 2.97 us | 336,267 | 439 B |
| JSON byte array | 4.93 us | 20.59 us | 25.52 us | 39,183 | 608 B |
| JSON base64 | 1.91 us | 7.28 us | 9.19 us | 108,778 | 467 B |

### 100 Point3d objects

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 3.28 us | 15.30 us | 18.58 us | 53,816 | 2.70 KiB |
| C++ headers (scalar) | 3.58 us | 15.06 us | 18.64 us | 53,641 | 2.70 KiB |
| v8serial addon | 57.28 us | 46.73 us | 104.01 us | 9,615 | 2.70 KiB |
| Node v8 | 7.59 us | 15.94 us | 23.53 us | 42,494 | 3.82 KiB |
| JSON text | 11.60 us | 11.67 us | 23.27 us | 42,968 | 2.67 KiB |

### 4 KiB Latin-1 string

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 359 ns | 239 ns | 599 ns | 1,669,859 | 4.00 KiB |
| C++ headers (scalar) | 2.15 us | 1.22 us | 3.37 us | 296,751 | 4.00 KiB |
| v8serial addon | 1.28 us | 803 ns | 2.08 us | 480,749 | 4.00 KiB |
| Node v8 | 369 ns | 324 ns | 693 ns | 1,443,027 | 4.00 KiB |
| JSON text | 8.60 us | 21.99 us | 30.59 us | 32,689 | 8.00 KiB |

### 1 KiB blob

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 69 ns | 190 ns | 258 ns | 3,869,939 | 1.03 KiB |
| C++ headers (scalar) | 71 ns | 183 ns | 253 ns | 3,947,668 | 1.03 KiB |
| v8serial addon | 1.06 us | 673 ns | 1.73 us | 578,297 | 1.03 KiB |
| Node v8 | 652 ns | 474 ns | 1.13 us | 888,209 | 1.02 KiB |
| JSON byte array | 50.50 us | 204.16 us | 254.66 us | 3,927 | 3.60 KiB |
| JSON base64 | 3.34 us | 1.99 us | 5.33 us | 187,595 | 1.37 KiB |

### 1 MiB blob

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 20.45 us | 27.27 us | 47.71 us | 20,960 | 1.00 MiB |
| C++ headers (scalar) | 18.99 us | 27.03 us | 46.02 us | 21,730 | 1.00 MiB |
| v8serial addon | 86.35 us | 113.79 us | 200.14 us | 4,996 | 1.00 MiB |
| Node v8 | 15.55 us | 1.12 us | 16.66 us | 60,015 | 1.00 MiB |
| JSON byte array | 69.91 ms | 430.40 ms | 500.32 ms | 2 | 3.57 MiB |
| JSON base64 | 2.91 ms | 808.01 us | 3.72 ms | 269 | 1.33 MiB |

## Interpretation

The direct C++ figures represent the intended worker-thread use case: native code writes or reads the header-only value model without first crossing the JavaScript object boundary. Addon figures intentionally include that boundary and therefore isolate its cost.

JSON text is highly optimized for small plain JavaScript values. Binary payloads require an additional representation: decimal byte arrays preserve bytes without base64 but greatly increase size and CPU work, while base64 adds approximately one-third wire-size overhead plus conversion cost.

Raw measurements, including all JavaScript timing samples, are stored in [`../bench/results.json`](../bench/results.json).

