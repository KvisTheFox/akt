# CLI

## aktc

```text
aktc build <file.akt> [-o out] [--release] [--keep-c] [--cc cc]
                      [--shared] [--target triple] [--sysroot dir]
                      [-I dir] [-L dir] [-l lib] [-D name[=value]]
aktc run <file.akt> [--release]
aktc check <file.akt>
aktc --version
```

Environment: `AKT_ROOT`, `AKT_CC`, `AKT_DEBUG`.

## katek

```text
katek new <name> [--bare]
katek build [--release]
katek run [file.akt] [--release]
katek check
katek test
katek clean
katek doctor
katek lock
katek export xmake|cmake
katek c add <name> [--provider p] [--version v] [--package pkg]
katek c install [--dry-run]
katek c sync
katek --version
```

`katek` discovers a project by walking up to `katek.toml` or
`src/main.akt`. Dependencies live in `[dependencies]`:

```toml
[dependencies]
glfw = "3.4"                        # xrepo by default
sqlite3 = { provider = "system" }   # system library
curl = { provider = "pkg-config", package = "libcurl" }
```

`katek` resolves include/library paths and links the libraries itself;
`[native] link` is no longer required. The older
`[native.dependencies]` section is still supported.

Build scripts:

- `build.akt` in the project root runs before the build. It can generate
  code or resources and write extra compiler flags to
  `.kache/extra.flags`;
- `[build] pre = "..."` and `[build] post = "..."` run shell commands
  before and after compilation.

```toml
[build]
pre = "python tools/gen_assets.py"
```

`katek build` produces `target/app.exe` (`target/app` on Unix); with
`--shared` or a `--lib` project it produces `target/app.dll` on Windows
and `target/libapp.so` elsewhere. Generated C and header caches live in
`.kache/`.

`katek doctor` reports whether Clang, `aktc`, and the runtime are found.

`katek export xmake|cmake` generates a native build file for the current
project and keeps `src/main.gen.c`.

Android:

```bash
katek build --target android-arm64 --release
```

See [ANDROID.md](ANDROID.md).

## Installer

```powershell
bin\akt-setup.exe
bin\akt-setup.exe --dry-run
```

The installer copies the toolchain to `%LOCALAPPDATA%\akt`, adds
`...\akt\bin` to the user PATH, and installs LLVM via winget if Clang
is missing.
