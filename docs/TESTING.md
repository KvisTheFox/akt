# Testing

## Running the suites

```powershell
# Windows
powershell -ExecutionPolicy Bypass -File tests/run.ps1
powershell -ExecutionPolicy Bypass -File tests/fuzz.ps1
powershell -ExecutionPolicy Bypass -File tests/asan.ps1
```

```bash
# Linux / macOS (pwsh required for the scripts)
pwsh tests/run.ps1
```

`tests/run.ps1` builds every `tests/lang/*.akt` with `aktc` and compares
stdout with the matching `.out` file. Then it checks that every
`tests/negative/*.akt` is rejected by the compiler with a non-zero exit
code.

## Language tests

| Test | Covers |
|---|---|
| `hello` | entry point, `println` |
| `arithmetic` | integer/float operators, precedence |
| `decl_order` | declarations at first use, nested scopes |
| `strings` | literals, concatenation, comparison, formatting |
| `fstring_prec` | f-strings and `{value:.N}` |
| `globals` | top-level variables without `static`, global arrays |
| `garr` | global fixed arrays, indexing, assignment |
| `aliases` | `type` aliases in fields and locals |
| `structs` | structs, methods, `static fn`, `self` |
| `switch` | switch statements, ranges, fallthrough-free cases |
| `switch_expr` | switch expressions |
| `payload_enum` | enums with payloads, pattern matching |
| `option_result` | `Option`, `Result`, `?` |
| `result_main` | `Result`-returning entry points and exit codes |
| `map` | `Map` operations |
| `set_deque` | `Set` and `Deque` |
| `garr` | global fixed arrays, indexing, assignment |
| `closures` | closures in `filter`/`map`/`reduce` |
| `defer` | LIFO cleanup on block exit and return |
| `arena` | arena allocation and release |
| `shared` | `shared` cells and refcounting |
| `weak` / `weak_upgrade` | weak cells and `upgrade()` to `Option` |
| `thread` / `thread_capture` | `thread.spawn/join`, captures |
| `sync_atomic` | `Mutex`, `Atomic<T>` |
| `async`, `async_all`, `async_real`, `async_timeout`, `async_deferred` | tasks, `all/select/timeout`, deferred start |
| `modules` | multi-file modules, `use` |
| `fib` | recursion, release performance smoke test |
| `json` | JSON parse/serialize |
| `regex` | regex subset |
| `http` | HTTP client through curl |
| `sqlite` | SQLite through the system library |
| `std_extra` | hash, base64, random, time, path |
| `c_native` | `cinclude <string.h>` with no declarations |
| `native_header` | imported struct, enum, `#define`, typedef |
| `abi_check` | imported `div_t`, ABI `_Static_assert` |
| `cimport` | header + sibling `.c` compiled automatically |
| `c_interop`, `c_interop_decl`, `c_manual` | manual `cfn`/`cstruct` |
| `callbacks` | C callbacks and function pointers |
| `opengl_probe` | linking `opengl32` |

## Negative tests

`break_outside_loop`, `return_local_ref`, `return_field_ref`,
`type_mismatch`, `unknown_field`, `unknown_name`, `variadic_fn` —
each must fail to compile.

## Fuzzing

`tests/fuzz.ps1` feeds random token streams to the parser and requires
that it never crashes (errors are fine).

## Sanitizers

`tests/asan.ps1` builds a subset of the tests with
`-fsanitize=address,undefined` and runs them.

## Benchmarks

See [../benchmarks/README.md](../benchmarks/README.md) and
[../benchmarks/RESULTS.md](../benchmarks/RESULTS.md).
