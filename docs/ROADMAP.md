# Roadmap

Current version: 1.0.0.

## Done

- compiler frontend and C11 backend
- functions, structs, methods, enums, switch
- arrays, Vector/Map/Set/Deque, strings, f-strings with `{x:.2}`
- Option/Result, `?`, defer, arenas, modules
- closures in HOFs and `thread.spawn` capture
- async/await (`async.all/select/timeout`), deferred task start
- shared/weak with `weak.upgrade()`
- Mutex, Atomic<T>, threads
- C interop: `cinclude`, `clink`, `cfn` (variadic and callbacks),
  `cstruct`/`cunion`, native `cinclude` import, `.kache` build cache,
  ABI `_Static_assert`, `-I/-D/-L/-l` from the manifest
- type aliases (`type X = T`)
- runtime: strings, vectors, map/set, option/result, arena, files,
  env, path, time, random, hash, base64, JSON, regex, HTTP, SQLite
- katek: new/build/run/check/test/clean/doctor/lock/jni,
  c add/sync/install, export xmake|cmake, Android NDK
- Windows installer, benchmarks vs C/C++/Rust, RU/EN docs
- tests, fuzzing, ASan/UBSan, CI

## Beyond 1.0

1. generics beyond built-in containers
2. a dedicated async executor
3. borrow checking for `^T`/`weak`
4. bitfields and complex declarators in the header importer
5. package registry

## 1.0 criteria — met

- real programs (HTTP client, CLI, JSON parser, SQLite wrapper) in akt
- no known critical bugs; fuzzing and sanitizers pass
- reproducible builds with lock file
- stable runtime ABI with `_Static_assert` checks
- complete RU/EN docs and examples
