# Performance

Generated 2026-08-27T15:22:26.987Z on Apple M4 Max (arm64), Node v22.20.0, V8 12.4.254.21-node.33.

![Performance comparison](performance.svg)

## Methodology

- Seven timed rounds per operation; tables report the median nanoseconds per operation.
- Every operation is warmed up first and its result is consumed.
- Encode and decode are measured separately; round trip is the sum of their medians.
- The writer-reuse comparison measures only C++ encoding: the fresh path constructs and destroys a capacity-reserved `Writer` per message, while the reuse path keeps one `Writer` and calls `reset()`. Both consume `size()` and exclude copying bytes to a downstream consumer.
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
- For geometry with a 64-byte blob, direct C++ headers are 4.3x faster than JSON base64. The generic addon bridge is 1.1x the JSON-base64 latency because JavaScript property traversal dominates this small payload.
- For a 1 MiB blob, direct C++ headers are 86.3x faster and the addon bridge is 19.5x faster than JSON base64, while avoiding base64's wire-size expansion.
- Node V8 is exceptionally fast for large typed arrays because its host-object deserializer may return a view into the serialized input; the standalone reader instead returns owning native bytes.
- On this run, reusing one reserved `Writer` with `reset()` measured 942.7% higher latency for scalar encoding and 10.2% lower for nested geometry versus constructing and destroying a writer for every message.

## Writer buffer reuse

`reset()` clears message state and re-emits the wire header while retaining the writer buffer capacity. The table isolates that lifecycle benefit; it does not include copying the completed bytes into a queue, socket, or consumer-owned buffer. See the [writer guide](writer.md#reusing-buffer-capacity) for the ownership rules.

| Build | Payload | Fresh writer | Reused writer | Latency change | Speedup | Wire size |
|---|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | Scalar int32 | 0 ns | 3 ns | 942.7% higher | 0.10x | 4 B |
| C++ headers (scalar) | Scalar int32 | 0 ns | 3 ns | 962.6% higher | 0.09x | 4 B |
| C++ headers (SIMD) | Nested geometry | 379 ns | 340 ns | 10.2% lower | 1.11x | 359 B |
| C++ headers (scalar) | Nested geometry | 403 ns | 365 ns | 9.5% lower | 1.11x | 359 B |

## Results

### Scalar int32

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 28 ns | 11 ns | 39 ns | 25,853,822 | 4 B |
| C++ headers (scalar) | 28 ns | 12 ns | 40 ns | 25,224,625 | 4 B |
| v8serial addon | 251 ns | 75 ns | 326 ns | 3,064,685 | 4 B |
| Node v8 | 282 ns | 223 ns | 506 ns | 1,977,187 | 4 B |
| JSON text | 81 ns | 84 ns | 166 ns | 6,037,138 | 2 B |

### Point3d

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 61 ns | 170 ns | 231 ns | 4,332,138 | 41 B |
| C++ headers (scalar) | 67 ns | 162 ns | 230 ns | 4,355,165 | 41 B |
| v8serial addon | 849 ns | 496 ns | 1.35 us | 743,477 | 41 B |
| Node v8 | 479 ns | 465 ns | 943 ns | 1,060,213 | 41 B |
| JSON text | 242 ns | 241 ns | 483 ns | 2,070,292 | 31 B |

### Nested geometry

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 372 ns | 1.43 us | 1.80 us | 554,135 | 359 B |
| C++ headers (scalar) | 403 ns | 1.40 us | 1.80 us | 555,808 | 359 B |
| v8serial addon | 5.54 us | 4.23 us | 9.77 us | 102,332 | 359 B |
| Node v8 | 1.00 us | 1.67 us | 2.67 us | 374,423 | 366 B |
| JSON text | 939 ns | 1.26 us | 2.20 us | 453,866 | 353 B |

### Geometry + 64 B

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 507 ns | 1.68 us | 2.18 us | 458,206 | 436 B |
| C++ headers (scalar) | 397 ns | 1.46 us | 1.86 us | 537,737 | 436 B |
| v8serial addon | 5.75 us | 4.69 us | 10.44 us | 95,799 | 436 B |
| Node v8 | 1.27 us | 1.85 us | 3.12 us | 320,637 | 439 B |
| JSON byte array | 5.01 us | 20.57 us | 25.58 us | 39,088 | 608 B |
| JSON base64 | 1.95 us | 7.38 us | 9.33 us | 107,183 | 467 B |

### 100 Point3d objects

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 3.53 us | 17.85 us | 21.38 us | 46,764 | 2.70 KiB |
| C++ headers (scalar) | 3.34 us | 17.08 us | 20.42 us | 48,966 | 2.70 KiB |
| v8serial addon | 62.47 us | 47.87 us | 110.34 us | 9,063 | 2.70 KiB |
| Node v8 | 7.73 us | 16.41 us | 24.14 us | 41,431 | 3.82 KiB |
| JSON text | 11.65 us | 11.53 us | 23.18 us | 43,137 | 2.67 KiB |

### 4 KiB Latin-1 string

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 376 ns | 258 ns | 634 ns | 1,577,215 | 4.00 KiB |
| C++ headers (scalar) | 2.26 us | 1.26 us | 3.53 us | 283,647 | 4.00 KiB |
| v8serial addon | 1.34 us | 812 ns | 2.15 us | 465,655 | 4.00 KiB |
| Node v8 | 427 ns | 375 ns | 803 ns | 1,245,976 | 4.00 KiB |
| JSON text | 8.78 us | 21.08 us | 29.86 us | 33,492 | 8.00 KiB |

### 1 KiB blob

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 76 ns | 207 ns | 283 ns | 3,536,512 | 1.03 KiB |
| C++ headers (scalar) | 73 ns | 181 ns | 254 ns | 3,936,524 | 1.03 KiB |
| v8serial addon | 1.04 us | 731 ns | 1.77 us | 565,999 | 1.03 KiB |
| Node v8 | 706 ns | 549 ns | 1.25 us | 796,959 | 1.02 KiB |
| JSON byte array | 51.62 us | 209.49 us | 261.11 us | 3,830 | 3.60 KiB |
| JSON base64 | 3.44 us | 2.01 us | 5.45 us | 183,567 | 1.37 KiB |

### 1 MiB blob

| Codec | Encode | Decode | Round trip | Operations/s | Wire size |
|---|---:|---:|---:|---:|---:|
| C++ headers (SIMD) | 18.53 us | 25.34 us | 43.87 us | 22,794 | 1.00 MiB |
| C++ headers (scalar) | 16.67 us | 25.68 us | 42.36 us | 23,609 | 1.00 MiB |
| v8serial addon | 82.26 us | 112.19 us | 194.45 us | 5,143 | 1.00 MiB |
| Node v8 | 15.80 us | 945 ns | 16.75 us | 59,709 | 1.00 MiB |
| JSON byte array | 72.37 ms | 433.43 ms | 505.79 ms | 2 | 3.57 MiB |
| JSON base64 | 2.97 ms | 820.36 us | 3.79 ms | 264 | 1.33 MiB |

## Interpretation

The direct C++ figures represent the intended worker-thread use case: native code writes or reads the header-only value model without first crossing the JavaScript object boundary. Addon figures intentionally include that boundary and therefore isolate its cost.

JSON text is highly optimized for small plain JavaScript values. Binary payloads require an additional representation: decimal byte arrays preserve bytes without base64 but greatly increase size and CPU work, while base64 adds approximately one-third wire-size overhead plus conversion cost.

Raw measurements, including all JavaScript timing samples, are stored in [`../bench/results.json`](../bench/results.json).

