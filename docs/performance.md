# Performance

Generated 2026-08-25T13:47:29.022Z on Apple M4 Max (arm64), Node v22.20.0, V8 12.4.254.21-node.33.

![Performance comparison](performance.svg)

## Methodology

- Seven timed rounds per operation; tables report the median nanoseconds per operation.
- Every operation is warmed up first and its result is consumed.
- Encode and decode are measured separately; round trip is the sum of their medians.
- `C++ headers` calls `Writer` and `Reader` directly with native values and has no JavaScript/N-API traversal.
- `v8serial addon` includes generic JavaScript object traversal and N-API boundary cost.
- `Node v8` is Node.js `v8.serialize()` and `v8.deserialize()`.
- `JSON text` applies only to payloads without binary values.
- `JSON byte array` preserves Uint8Array as JSON decimal byte arrays.
- `JSON base64` preserves Uint8Array using base64 text and includes conversion in both timings.
- Results are machine-specific; regenerate with `npm run bench`.
- The C++ reader returns owning strings and byte vectors. Node may reconstruct a host-object typed array as a view into the serialized input, so its large-blob decode has different ownership semantics.

## Highlights

- JSON text is fastest for scalar and small plain JavaScript values because V8 has highly optimized built-in JSON paths.
- For geometry with a 64-byte blob, direct C++ headers are 4.8x faster than JSON base64. The generic addon bridge is 1.1x the JSON-base64 latency because JavaScript property traversal dominates this small payload.
- For a 1 MiB blob, direct C++ headers are 77.4x faster and the addon bridge is 21.7x faster than JSON base64, while avoiding base64's wire-size expansion.
- Node V8 is exceptionally fast for large typed arrays because its host-object deserializer may return a view into the serialized input; the standalone reader instead returns owning native bytes.

## Results

### Scalar int32

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers | 29 ns | 10 ns | 39 ns | 25,945,735 | 4 B |
| v8serial addon | 410 ns | 69 ns | 478 ns | 2,090,629 | 4 B |
| Node v8 | 304 ns | 207 ns | 512 ns | 1,954,250 | 4 B |
| JSON text | 78 ns | 83 ns | 161 ns | 6,220,039 | 2 B |

### Point3d

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers | 62 ns | 143 ns | 205 ns | 4,876,139 | 41 B |
| v8serial addon | 962 ns | 502 ns | 1.46 us | 683,024 | 41 B |
| Node v8 | 515 ns | 428 ns | 942 ns | 1,061,080 | 41 B |
| JSON text | 231 ns | 238 ns | 469 ns | 2,132,141 | 31 B |

### Nested geometry

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers | 333 ns | 1.52 us | 1.86 us | 538,938 | 359 B |
| v8serial addon | 5.16 us | 4.49 us | 9.65 us | 103,609 | 359 B |
| Node v8 | 1.10 us | 1.54 us | 2.64 us | 379,309 | 366 B |
| JSON text | 909 ns | 1.19 us | 2.10 us | 477,074 | 353 B |

### Geometry + 64 B

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers | 353 ns | 1.59 us | 1.95 us | 513,591 | 436 B |
| v8serial addon | 5.52 us | 4.75 us | 10.27 us | 97,378 | 436 B |
| Node v8 | 1.31 us | 1.71 us | 3.02 us | 331,385 | 439 B |
| JSON byte array | 4.88 us | 20.37 us | 25.25 us | 39,605 | 608 B |
| JSON base64 | 1.85 us | 7.50 us | 9.35 us | 106,963 | 467 B |

### 100 Point3d objects

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers | 3.14 us | 17.19 us | 20.33 us | 49,182 | 2.70 KiB |
| v8serial addon | 58.76 us | 44.14 us | 102.90 us | 9,718 | 2.70 KiB |
| Node v8 | 7.36 us | 16.03 us | 23.39 us | 42,754 | 3.82 KiB |
| JSON text | 11.22 us | 12.27 us | 23.49 us | 42,574 | 2.67 KiB |

### 1 KiB blob

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers | 75 ns | 187 ns | 262 ns | 3,815,179 | 1.03 KiB |
| v8serial addon | 1.17 us | 760 ns | 1.93 us | 519,106 | 1.03 KiB |
| Node v8 | 729 ns | 549 ns | 1.28 us | 782,838 | 1.02 KiB |
| JSON byte array | 50.69 us | 205.49 us | 256.18 us | 3,903 | 3.60 KiB |
| JSON base64 | 3.19 us | 1.89 us | 5.08 us | 196,895 | 1.37 KiB |

### 1 MiB blob

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers | 20.73 us | 29.96 us | 50.69 us | 19,727 | 1.00 MiB |
| v8serial addon | 78.69 us | 102.33 us | 181.01 us | 5,524 | 1.00 MiB |
| Node v8 | 14.71 us | 1.11 us | 15.82 us | 63,224 | 1.00 MiB |
| JSON byte array | 70.38 ms | 430.33 ms | 500.71 ms | 2 | 3.57 MiB |
| JSON base64 | 3.11 ms | 813.80 us | 3.92 ms | 255 | 1.33 MiB |

## Interpretation

The direct C++ figures represent the intended worker-thread use case: native code writes or reads the header-only value model without first crossing the JavaScript object boundary. Addon figures intentionally include that boundary and therefore isolate its cost.

JSON text is highly optimized for small plain JavaScript values. Binary payloads require an additional representation: decimal byte arrays preserve bytes without base64 but greatly increase size and CPU work, while base64 adds approximately one-third wire-size overhead plus conversion cost.

Raw measurements, including all JavaScript timing samples, are stored in [`../bench/results.json`](../bench/results.json).

