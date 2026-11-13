# Contributing to akt

Thanks for helping! The project is small and the workflow is simple.

## Build

```bash
# Windows
xmake f -c --toolchain=clang-cl -m release
xmake build

# Linux / macOS
xmake f -c -m release
xmake build
```

Binaries go to `bin/`.

## Test

```powershell
powershell -ExecutionPolicy Bypass -File tests/run.ps1
powershell -ExecutionPolicy Bypass -File tests/fuzz.ps1
powershell -ExecutionPolicy Bypass -File tests/asan.ps1
```

Add a test for every behavior change:

- `tests/lang/<name>.akt` plus `<name>.out` for positive tests;
- `tests/negative/<name>.akt` for code that must be rejected;
- update [docs/TESTING.md](docs/TESTING.md) when adding a test group.

## Layout

- `aktc/src/lexer.c` — tokens;
- `aktc/src/parser.c` — AST;
- `aktc/src/checker.c` — types, scopes, diagnostics;
- `aktc/src/codegen.c` — C11 output;
- `aktc/src/cimport.c` — C header importer;
- `runtime/` — C runtime;
- `katek/src/main.c` — build tool.

## Style

- C11, no compiler-specific extensions outside `#ifdef` blocks;
- keep error messages lowercase and specific;
- generated C must build with Clang and GCC warnings enabled;
- no `vendor/` directories, no generated files in the source tree —
  everything goes to `.kache/`.

## Commits

Small, focused commits with a clear subject. Mention the test you added.

## License

By contributing you agree that your changes are licensed under the
GPL-3.0-or-later with the akt Output Exception (see [LICENSE](LICENSE)
and [LICENSE-EXCEPTION](LICENSE-EXCEPTION)).
