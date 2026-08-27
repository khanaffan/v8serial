# Performance

Generated 2026-08-27T15:31:52.479Z on Apple M4 Max (arm64), Node v22.20.0, V8 12.4.254.21-node.33.

![Performance comparison](performance.svg)

## Methodology

- Seven timed rounds per operation; tables report the median nanoseconds per operation.
- Every operation is warmed up first and its result is consumed.
- Encode and decode are measured separately; round trip is the sum of their medians.
- The writer-reuse comparison measures only C++ encoding: the one-shot path constructs a capacity-reserved `Writer`, moves its vector out with `take()`, and destroys that vector per message; the reuse path keeps one `Writer`, calls `reset()`, and consumes `size()`. Neither path copies the completed payload bytes to a downstream consumer.
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
- SIMD makes the 4 KiB Latin-1 native round trip 5.5x faster than the scalar path.
- For geometry with a 64-byte blob, direct C++ headers are 4.8x faster than JSON base64. The generic addon bridge is 1.1x the JSON-base64 latency because JavaScript property traversal dominates this small payload.
- For a 1 MiB blob, direct C++ headers are 91.4x faster and the addon bridge is 19.1x faster than JSON base64, while avoiding base64's wire-size expansion.
- Node V8 is exceptionally fast for large typed arrays because its host-object deserializer may return a view into the serialized input; the standalone reader instead returns owning native bytes.
- On this run, reusing one reserved `Writer` with `reset()` measured 87.4% lower latency for scalar encoding and 5.8% lower for nested geometry versus constructing and destroying a writer for every message.

## Writer buffer reuse

`reset()` clears message state and re-emits the wire header while retaining the writer buffer capacity. The table isolates that lifecycle benefit; it does not include copying the completed bytes into a queue, socket, or consumer-owned buffer. See the [writer guide](writer.md#reusing-buffer-capacity) for the ownership rules.

| Build | Payload | Fresh writer | Reused writer | Latency change | Speedup | Wire size |
|---|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | Scalar int32 | 25 ns | 3 ns | 87.4% lower | 7.93x | 4 B |
| C++ headers (scalar) | Scalar int32 | 29 ns | 3 ns | 89.7% lower | 9.66x | 4 B |
| C++ headers (SIMD) | Nested geometry | 356 ns | 335 ns | 5.8% lower | 1.06x | 359 B |
| C++ headers (scalar) | Nested geometry | 409 ns | 369 ns | 9.7% lower | 1.11x | 359 B |

## Results

### Scalar int32

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 25 ns | 12 ns | 37 ns | 26,901,028 | 4 B |
| C++ headers (scalar) | 29 ns | 11 ns | 40 ns | 25,089,947 | 4 B |
| v8serial addon | 256 ns | 81 ns | 337 ns | 2,966,104 | 4 B |
| Node v8 | 289 ns | 213 ns | 502 ns | 1,993,478 | 4 B |
| JSON text | 75 ns | 82 ns | 156 ns | 6,397,560 | 2 B |

### Point3d

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 62 ns | 188 ns | 250 ns | 3,998,644 | 41 B |
| C++ headers (scalar) | 66 ns | 163 ns | 229 ns | 4,363,101 | 41 B |
| v8serial addon | 833 ns | 531 ns | 1.36 us | 732,906 | 41 B |
| Node v8 | 464 ns | 442 ns | 905 ns | 1,104,489 | 41 B |
| JSON text | 234 ns | 244 ns | 478 ns | 2,092,188 | 31 B |

### Nested geometry

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 421 ns | 1.68 us | 2.10 us | 475,828 | 359 B |
| C++ headers (scalar) | 402 ns | 1.46 us | 1.87 us | 535,957 | 359 B |
| v8serial addon | 5.57 us | 4.17 us | 9.74 us | 102,640 | 359 B |
| Node v8 | 1.03 us | 1.64 us | 2.68 us | 373,456 | 366 B |
| JSON text | 936 ns | 1.25 us | 2.18 us | 458,225 | 353 B |

### Geometry + 64 B

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 402 ns | 1.60 us | 2.00 us | 499,854 | 436 B |
| C++ headers (scalar) | 428 ns | 1.56 us | 1.99 us | 503,215 | 436 B |
| v8serial addon | 5.84 us | 4.46 us | 10.30 us | 97,106 | 436 B |
| Node v8 | 1.24 us | 1.82 us | 3.06 us | 326,715 | 439 B |
| JSON byte array | 5.01 us | 20.65 us | 25.66 us | 38,974 | 608 B |
| JSON base64 | 1.93 us | 7.58 us | 9.52 us | 105,089 | 467 B |

### 100 Point3d objects

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 3.45 us | 16.77 us | 20.23 us | 49,438 | 2.70 KiB |
| C++ headers (scalar) | 3.32 us | 17.51 us | 20.83 us | 48,001 | 2.70 KiB |
| v8serial addon | 61.11 us | 47.77 us | 108.88 us | 9,184 | 2.70 KiB |
| Node v8 | 7.76 us | 16.47 us | 24.23 us | 41,271 | 3.82 KiB |
| JSON text | 11.19 us | 12.02 us | 23.21 us | 43,086 | 2.67 KiB |

### 4 KiB Latin-1 string

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 365 ns | 247 ns | 612 ns | 1,634,981 | 4.00 KiB |
| C++ headers (scalar) | 2.18 us | 1.18 us | 3.36 us | 297,514 | 4.00 KiB |
| v8serial addon | 1.23 us | 796 ns | 2.03 us | 493,461 | 4.00 KiB |
| Node v8 | 402 ns | 369 ns | 771 ns | 1,296,226 | 4.00 KiB |
| JSON text | 9.20 us | 19.94 us | 29.14 us | 34,320 | 8.00 KiB |

### 1 KiB blob

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 69 ns | 176 ns | 245 ns | 4,074,976 | 1.03 KiB |
| C++ headers (scalar) | 73 ns | 192 ns | 265 ns | 3,777,088 | 1.03 KiB |
| v8serial addon | 1.04 us | 717 ns | 1.76 us | 569,479 | 1.03 KiB |
| Node v8 | 700 ns | 521 ns | 1.22 us | 818,461 | 1.02 KiB |
| JSON byte array | 50.31 us | 213.33 us | 263.63 us | 3,793 | 3.60 KiB |
| JSON base64 | 3.29 us | 2.01 us | 5.30 us | 188,810 | 1.37 KiB |

### 1 MiB blob

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 18.04 us | 25.61 us | 43.65 us | 22,910 | 1.00 MiB |
| C++ headers (scalar) | 19.44 us | 28.22 us | 47.66 us | 20,982 | 1.00 MiB |
| v8serial addon | 84.43 us | 124.85 us | 209.28 us | 4,778 | 1.00 MiB |
| Node v8 | 18.94 us | 990 ns | 19.93 us | 50,170 | 1.00 MiB |
| JSON byte array | 70.89 ms | 440.14 ms | 511.03 ms | 2 | 3.57 MiB |
| JSON base64 | 3.12 ms | 874.67 us | 3.99 ms | 251 | 1.33 MiB |

## Interpretation

The direct C++ figures represent the intended worker-thread use case: native code writes or reads the header-only value model without first crossing the JavaScript object boundary. Addon figures intentionally include that boundary and therefore isolate its cost.

JSON text is highly optimized for small plain JavaScript values. Binary payloads require an additional representation: decimal byte arrays preserve bytes without base64 but greatly increase size and CPU work, while base64 adds approximately one-third wire-size overhead plus conversion cost.

Raw measurements, including all JavaScript timing samples, are stored in [`../bench/results.json`](../bench/results.json).

