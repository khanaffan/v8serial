# Performance

Generated 2026-08-27T15:37:14.531Z on Apple M4 Max (arm64), Node v22.20.0, V8 12.4.254.21-node.33.

![Performance comparison](performance.svg)

## Methodology

- Seven timed rounds per operation; tables report the median nanoseconds per operation.
- Every operation is warmed up first and its result is consumed.
- Encode and decode are measured separately; round trip is the sum of their medians.
- The writer-reuse comparison measures only C++ encoding: the one-shot path constructs a capacity-reserved `Writer`, moves its vector out with `take()`, and destroys that vector per message; the reuse path keeps one `Writer` and calls `reset()`. Both paths consume representative output bytes to prevent compiler elision, but neither copies the completed payload to a downstream consumer.
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
- SIMD makes the 4 KiB Latin-1 native round trip 5.3x faster than the scalar path.
- For geometry with a 64-byte blob, direct C++ headers are 4.7x faster than JSON base64. The generic addon bridge is 1.1x the JSON-base64 latency because JavaScript property traversal dominates this small payload.
- For a 1 MiB blob, direct C++ headers are 91.3x faster and the addon bridge is 18.9x faster than JSON base64, while avoiding base64's wire-size expansion.
- Node V8 is exceptionally fast for large typed arrays because its host-object deserializer may return a view into the serialized input; the standalone reader instead returns owning native bytes.
- On this run, reusing one reserved `Writer` with `reset()` measured 81.9% lower latency for scalar encoding and 7.8% lower for nested geometry versus constructing and destroying a writer for every message.

## Writer buffer reuse

`reset()` clears message state and re-emits the wire header while retaining the writer buffer capacity. The table isolates that lifecycle benefit; it does not include copying the completed bytes into a queue, socket, or consumer-owned buffer. See the [writer guide](writer.md#reusing-buffer-capacity) for the ownership rules.

| Build | Payload | One-shot `take()` | Reused `reset()` | Latency change | Speedup | Wire size |
|---|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | Scalar int32 | 28 ns | 5 ns | 81.9% lower | 5.54x | 4 B |
| C++ headers (scalar) | Scalar int32 | 27 ns | 5 ns | 81.9% lower | 5.54x | 4 B |
| C++ headers (SIMD) | Nested geometry | 383 ns | 353 ns | 7.8% lower | 1.08x | 359 B |
| C++ headers (scalar) | Nested geometry | 368 ns | 340 ns | 7.6% lower | 1.08x | 359 B |

## Results

### Scalar int32

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 28 ns | 10 ns | 38 ns | 26,229,921 | 4 B |
| C++ headers (scalar) | 29 ns | 10 ns | 39 ns | 25,789,081 | 4 B |
| v8serial addon | 250 ns | 81 ns | 331 ns | 3,019,937 | 4 B |
| Node v8 | 292 ns | 230 ns | 522 ns | 1,917,157 | 4 B |
| JSON text | 80 ns | 86 ns | 166 ns | 6,024,530 | 2 B |

### Point3d

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 72 ns | 159 ns | 230 ns | 4,341,288 | 41 B |
| C++ headers (scalar) | 61 ns | 156 ns | 217 ns | 4,607,993 | 41 B |
| v8serial addon | 796 ns | 531 ns | 1.33 us | 753,366 | 41 B |
| Node v8 | 494 ns | 461 ns | 955 ns | 1,047,574 | 41 B |
| JSON text | 235 ns | 236 ns | 471 ns | 2,124,752 | 31 B |

### Nested geometry

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 396 ns | 1.39 us | 1.79 us | 558,977 | 359 B |
| C++ headers (scalar) | 385 ns | 1.40 us | 1.78 us | 560,458 | 359 B |
| v8serial addon | 5.34 us | 4.45 us | 9.79 us | 102,165 | 359 B |
| Node v8 | 1.01 us | 1.65 us | 2.66 us | 375,312 | 366 B |
| JSON text | 980 ns | 1.21 us | 2.19 us | 457,586 | 353 B |

### Geometry + 64 B

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 427 ns | 1.54 us | 1.96 us | 509,086 | 436 B |
| C++ headers (scalar) | 405 ns | 1.50 us | 1.91 us | 524,148 | 436 B |
| v8serial addon | 5.76 us | 4.76 us | 10.52 us | 95,063 | 436 B |
| Node v8 | 1.25 us | 1.75 us | 3.00 us | 333,470 | 439 B |
| JSON byte array | 4.90 us | 21.32 us | 26.22 us | 38,139 | 608 B |
| JSON base64 | 1.92 us | 7.35 us | 9.27 us | 107,862 | 467 B |

### 100 Point3d objects

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 3.61 us | 17.00 us | 20.61 us | 48,528 | 2.70 KiB |
| C++ headers (scalar) | 3.17 us | 17.50 us | 20.66 us | 48,391 | 2.70 KiB |
| v8serial addon | 64.44 us | 48.66 us | 113.09 us | 8,842 | 2.70 KiB |
| Node v8 | 7.45 us | 15.44 us | 22.89 us | 43,679 | 3.82 KiB |
| JSON text | 10.99 us | 10.88 us | 21.87 us | 45,734 | 2.67 KiB |

### 4 KiB Latin-1 string

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 377 ns | 277 ns | 654 ns | 1,528,790 | 4.00 KiB |
| C++ headers (scalar) | 2.30 us | 1.19 us | 3.50 us | 286,091 | 4.00 KiB |
| v8serial addon | 1.19 us | 813 ns | 2.00 us | 499,524 | 4.00 KiB |
| Node v8 | 426 ns | 356 ns | 782 ns | 1,278,622 | 4.00 KiB |
| JSON text | 9.17 us | 20.23 us | 29.39 us | 34,021 | 8.00 KiB |

### 1 KiB blob

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 75 ns | 182 ns | 256 ns | 3,901,110 | 1.03 KiB |
| C++ headers (scalar) | 67 ns | 176 ns | 242 ns | 4,126,225 | 1.03 KiB |
| v8serial addon | 988 ns | 708 ns | 1.70 us | 589,913 | 1.03 KiB |
| Node v8 | 677 ns | 511 ns | 1.19 us | 842,125 | 1.02 KiB |
| JSON byte array | 51.10 us | 212.32 us | 263.42 us | 3,796 | 3.60 KiB |
| JSON base64 | 3.36 us | 1.99 us | 5.35 us | 186,968 | 1.37 KiB |

### 1 MiB blob

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 14.51 us | 28.59 us | 43.10 us | 23,200 | 1.00 MiB |
| C++ headers (scalar) | 18.53 us | 26.08 us | 44.61 us | 22,418 | 1.00 MiB |
| v8serial addon | 87.74 us | 121.02 us | 208.76 us | 4,790 | 1.00 MiB |
| Node v8 | 15.67 us | 1.09 us | 16.75 us | 59,687 | 1.00 MiB |
| JSON byte array | 70.13 ms | 439.21 ms | 509.34 ms | 2 | 3.57 MiB |
| JSON base64 | 3.10 ms | 830.93 us | 3.94 ms | 254 | 1.33 MiB |

## Interpretation

The direct C++ figures represent the intended worker-thread use case: native code writes or reads the header-only value model without first crossing the JavaScript object boundary. Addon figures intentionally include that boundary and therefore isolate its cost.

JSON text is highly optimized for small plain JavaScript values. Binary payloads require an additional representation: decimal byte arrays preserve bytes without base64 but greatly increase size and CPU work, while base64 adds approximately one-third wire-size overhead plus conversion cost.

Raw measurements, including all JavaScript timing samples, are stored in [`../bench/results.json`](../bench/results.json).

