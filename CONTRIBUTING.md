# Contributing to v8serial

Contributions are welcome through GitHub issues and pull requests.

## Development setup

v8serial requires Node.js 22, a C++17 compiler, Python, and the platform build
tools required by `node-gyp`.

```sh
git clone https://github.com/khanaffan/v8serial.git
cd v8serial
npm install
npm test
```

## Making changes

- Keep changes focused and consistent with the existing C++17 and Node.js APIs.
- Add or update tests for behavior changes and bug fixes.
- Update the relevant guide under `docs/` when changing a public API, supported
  V8 feature, wire-format behavior, or performance claim.
- Run `npm test` before opening a pull request.

Pull requests should explain the problem, the chosen approach, and any
compatibility or performance impact. Link related issues when applicable.

For string-path changes, build with `npm run build`, then run
`build/Release/v8serial_native_bench --strings` (add `.exe` on Windows).
This covers short strings, SIMD boundaries, long Latin-1 strings, and UTF-16
fallbacks with a non-Latin-1 character at either end. Output columns are the
scenario, encode ns, decode ns, and wire bytes. The corresponding
`v8serial_native_bench_scalar --strings` executable disables both explicit SIMD
and compiler loop vectorization; it is not an autovectorized baseline.

## Reporting issues

Before opening an issue, check the existing issues and confirm the problem with
Node.js 22. Include the operating system, architecture, Node.js version, a
minimal reproduction, and the complete error output.
