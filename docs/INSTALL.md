# Installation

## Requirements

- 64-bit Windows 10+, Linux (x86_64) or macOS (x86_64/arm64).
- Clang (or GCC on Linux). The installers check for it and can offer to
  install LLVM.

## Windows

Run **`akt-1.0.0-setup.exe`**. It is a single self-contained installer;
no archive is needed.

The installer:

- extracts the toolchain to `%LOCALAPPDATA%\akt`;
- adds `%LOCALAPPDATA%\akt\bin` to the user `PATH`;
- checks for `clang`;
- if Clang is missing, asks whether to install LLVM and, on `y`, runs
  `winget install LLVM.LLVM` (or downloads the official LLVM installer
  as a fallback).

Non-interactive options:

```powershell
akt-1.0.0-setup.exe --quiet
akt-1.0.0-setup.exe --dry-run
akt-1.0.0-setup.exe --no-llvm --no-path --prefix D:\akt
```

## Linux / macOS

```bash
tar xf akt-1.0.0-src.tar.gz
cd akt-1.0.0-src
sh scripts/install.sh
```

The script builds the toolchain with xmake and installs it to
`$HOME/.local/akt`, with symlinks in `$HOME/.local/bin`.

Options:

```bash
sh scripts/install.sh --prefix /usr/local
sh scripts/install.sh --no-build
```

If Clang is missing, the script asks before using `apt`, `dnf`, `pacman`
or `brew` to install it.

Add the bin directory to your shell profile:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

## Build from source

```bash
# Windows (LLVM toolchain)
xmake f -c --toolchain=clang-cl -m release
xmake build

# Linux / macOS
xmake f -c -m release
xmake build
```

CMake is also supported:

```bash
cmake -S . -B build
cmake --build build --config Release
```

Binaries are written to `bin/`.

## Verify

```bash
aktc --version
katek --version
katek new hello
cd hello
katek run
```

## Uninstall

- Windows: remove `%LOCALAPPDATA%\akt` and its `PATH` entry.
- Linux/macOS: remove `$PREFIX/akt` and the symlinks in `$PREFIX/bin`.

## Troubleshooting

| Problem | Fix |
|---|---|
| `clang: command not found` | install LLVM and reopen the terminal |
| `xmake: command not found` | install xmake, or use the prebuilt Windows zip |
| `error: unknown type name` for a C type | make sure `cinclude`/`cimport` names the right header |
| build cache looks stale | delete `.kache/` in the project |
| Windows linker cannot find a library | add it to `[native] link` in `katek.toml` |
