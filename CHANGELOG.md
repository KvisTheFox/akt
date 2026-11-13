# Changelog

All notable changes to akt are documented here.

## 1.0.0 — 2026-11-13

First public release.

### Language

- C-like syntax, compiled to C11 and built with Clang/GCC.
- Functions, structs, methods, `static fn`, `impl`-style methods.
- Enums with payloads, `switch` statements and expressions.
- Fixed arrays, `Vector`, `Map`, `Set`, `Deque`.
- `Option<T>`, `Result<T, E>`, `?`.
- `defer`, arenas, manual memory, `shared` / `weak` with `weak.upgrade()`.
- Modules (`use a.b`), `type` aliases, f-strings with `{x:.2}`.
- Closures in `Vector.filter/map/reduce` and `thread.spawn`.
- `async` / `await`, `async.all`, `async.select`, `async.timeout`.
- `thread.spawn/join`, `Mutex`, `Atomic<T>`.
- Top-level variables without `static`.

### C interop

- `cinclude` imports C declarations at build time (typedefs, structs,
  unions, enums, numeric `#define`s, prototypes).
- `cimport` also compiles the sibling `.c` file (sokol-style libraries).
- Manual `cfn` / `cstruct` / `cunion` escape hatch.
- `_Static_assert` ABI size checks for header-backed structs.
- Libraries and include paths come from `katek.toml`
  (`xrepo`, `vcpkg`, `Conan`, `pkg-config`, `system`, `CMake`).

### Toolchain

- `aktc`: build/run/check, C11 generation, project cache in `.kache/`.
- `katek`: `new`, `build`, `run`, `check`, `test`, `clean`, `doctor`,
  `lock`, `jni`, `c add/sync/install`, `export xmake|cmake`,
  Android NDK targets.
- Generated C, imported headers and temp files live in `.kache/`;
  source trees stay clean.

### Runtime and std

- Strings, vectors, maps, sets, options, results, arenas.
- JSON, regex, HTTP (curl), SQLite, files, env, path, time, random,
  hash, base64.

### Quality

- 52 language tests, 7 negative tests.
- Parser fuzzing.
- ASan/UBSan test run.
- Benchmarks vs C, C++, Rust and Zig.

### Platforms

- Windows (installer, `akt-setup.exe`).
- Linux and macOS via `scripts/install.sh`.
- Android `.so` via `katek build --target android-arm64`.

### License

- akt: GPL-3.0-or-later with the akt Output Exception.
- Programs written in akt: any license, including closed source.
