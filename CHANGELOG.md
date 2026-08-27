# Changelog

Notable changes to v8serial are documented here.

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

[0.1.0]: https://github.com/khanaffan/v8serial/releases/tag/v0.1.0
