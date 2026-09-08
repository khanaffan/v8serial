# Changelog

Notable changes to v8serial are documented here.

## [1.0.0] - 2026-09-08

### Added

- `Reader::readRows()` streams a root array of positional scalar rows without
  building an owning `DecodedValue` tree. String payloads are borrowed spans
  into the serialized input.
- Bulk JavaScript/native boundary benchmarks (`npm run bench:boundary`) that
  separate N-API call amplification, object traversal, owning-tree decode, and
  streaming row consumption.
- Native string microbenchmarks (`v8serial_native_bench --strings`) covering
  short lengths, SIMD vector boundaries, long Latin-1 strings, and UTF-16
  fallbacks.

### Changed

- Native ArrayBuffer-view decode validates backing-buffer and view metadata,
  then copies only the selected byte range. Results still own their bytes and
  do not alias the serialized input.
- Documentation covers streaming decode, writer buffer reuse, boundary costs,
  and how to measure string-path changes.

### Performance

- On Apple M4 Max, the single-copy view path measured about 2.08x faster native
  decode for a 1 MiB blob versus the previous allocate-then-copy path. Published
  `npm run bench` tables remain the 2026-09-01 snapshot and are conservative for
  that workload.

## [0.1.0] - 2026-08-27

### Added

- Standalone C++17 writer and bounds-checked reader for the supported subset of
  V8 serialization format version 15.
- Synchronous and asynchronous Node.js addon APIs for encoding and decoding.
- Binary views, objects, arrays, strings, numeric values, dates, and malformed
  input validation documented in the supported-feature matrix.
- SIMD-accelerated Latin-1 string handling on AArch64 and x86-64.
- Native and Node.js tests, compatibility documentation, and performance
  benchmarks.

[1.0.0]: https://github.com/khanaffan/v8serial/releases/tag/v1.0.0
[0.1.0]: https://github.com/khanaffan/v8serial/releases/tag/v0.1.0
