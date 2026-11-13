# C libraries

## Current support

- `cinclude <header>` adds `#include`.
- `clink "lib"` adds `-llib`.
- `c.fn(args)` calls a C function.
- string arguments are passed as `const char*` (`.data`).
- `c.to_string(ptr)` and `c.cstring(ptr)` produce `string`.
- `katek.toml` supports `[native]` include/lib/link/defines.
- `system` and `pkg-config` dependencies add linker/include flags.
- `katek export xmake|cmake` generates a native build.

## Manifest

```toml
[native]
providers = ["xrepo", "pkg-config", "system"]
include = ["vendor/include"]
lib = ["vendor/lib"]
defines = ["APP_VERSION=\"1.0\""]
link = ["m"]

[native.dependencies]
zlib = { provider = "xrepo", version = "1.3.1" }
sqlite3 = { provider = "cmake", package = "SQLite3", version = "3.45" }
curl = { provider = "pkg-config", package = "libcurl", version = ">=8.0" }
sdl2 = { provider = "vcpkg", version = "2.30.0" }
mylib = { path = "vendor/mylib", build = "cmake" }
sdk = { path = "vendor/sdk", kind = "static" }
```

## Not yet implemented

- full xrepo/vcpkg/Conan/CMake resolution
- lock file with checksums
- automatic include/lib extraction from all providers

## Native C headers

`cinclude <header>` is parsed by `aktc` itself: typedefs, structs,
unions, enums, numeric `#define`s and prototypes are imported
automatically, so no bindgen step is required. Generated C and parsed
headers are cached in the project's `.kache/` directory; sources stay
clean. `cimport "lib.h"` also compiles the sibling `lib.c`.
`cfn`/`cstruct`/`cunion` remain as a manual escape hatch.

Dependencies come from `katek.toml`:

```toml
[native.dependencies]
glfw = { provider = "xrepo", version = "3.4" }
```

`katek build` resolves the installed xrepo package and adds `-I`/`-L`.

## Manual linking

```bash
aktc build src/main.akt --keep-c
clang -std=c11 -I runtime/include src/main.gen.c runtime/src/akt_runtime.c \
    -I /path/to/include -L /path/to/lib -lfoo -o app
```

XMake and CMake can build the toolchain itself. Provider integration for
third-party C repositories is on the roadmap.
