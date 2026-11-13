# Architecture

```text
.akt -> lexer -> parser -> checker -> C codegen -> clang/gcc -> exe
```

## Components

- `aktc/src/lexer.c` — tokens, strings, f-strings, comments.
- `aktc/src/parser.c` — recursive-descent AST.
- `aktc/src/checker.c` — scopes, names, types, Option/Result checks.
- `aktc/src/codegen.c` — C11 output, methods, vectors, defer, option/result.
- `runtime/` — strings, vectors, option/result, arena, files, paths, env,
  time, random, hash, base64, tracked allocations.
- `katek/` — project discovery, build/run/test, manifest flags, C deps.
- `installer/` — Windows setup: copies toolchain, checks Clang, updates PATH.

## AST

Expressions and statements are C structs with a `kind`, source location,
inferred `type`, and a union of payloads. The parser annotates the AST; the
checker fills `Expr.type`; codegen walks the typed AST.

## Codegen conventions

- methods: `Type_method(Type* self, ...)`
- static methods: `Type_method(...)`
- `Vector<T>`: `AktVec` + typed casts on access
- `Option<T>`: `AktOption`
- `Result<T,E>`: `AktResult`
- `defer`: LIFO cleanup emitted at block exit and before `return`
- `arena`: `AktArena` + `akt_arena_free` registered as a defer
- `main`: generated wrapper calls `akt_user_main` and then
  `akt_runtime_shutdown`

## Modules

`aktc` recursively loads `use a.b` files, parses each, and merges them
into one program. The current implementation uses a global namespace.
This is a deliberate first step; name mangling and visibility are future
work.

## Build

- XMake is the primary build system.
- CMake is the official alternative.
- The compiler itself is C11 and has no dependencies beyond libc.

## Extending

1. token in lexer/header
2. AST node
3. parser production
4. checker rule
5. C codegen
6. positive/negative test
7. docs and roadmap
