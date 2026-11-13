#!/bin/sh
# akt installer for Linux / macOS
#
#   sh scripts/install.sh [--prefix DIR] [--no-build]
#
# Builds the toolchain with xmake and installs:
#   DIR/akt/bin/aktc, DIR/akt/bin/katek, DIR/akt/runtime
#   DIR/bin/aktc, DIR/bin/katek          (symlinks)
#
# Default prefix: $HOME/.local

set -eu

PREFIX="${PREFIX:-$HOME/.local}"
BUILD=1

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) PREFIX="$2"; shift 2 ;;
        --no-build) BUILD=0; shift ;;
        -h|--help)
            echo "usage: sh scripts/install.sh [--prefix DIR] [--no-build]"
            exit 0
            ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

SRC=$(cd "$(dirname "$0")/.." && pwd)
cd "$SRC"

have_clang() {
    command -v clang >/dev/null 2>&1
}

install_clang() {
    if command -v apt-get >/dev/null 2>&1; then
        echo "sudo apt-get update && sudo apt-get install -y clang"
        sudo apt-get update && sudo apt-get install -y clang
    elif command -v dnf >/dev/null 2>&1; then
        echo "sudo dnf install -y clang"
        sudo dnf install -y clang
    elif command -v pacman >/dev/null 2>&1; then
        echo "sudo pacman -S --noconfirm clang"
        sudo pacman -S --noconfirm clang
    elif command -v brew >/dev/null 2>&1; then
        echo "brew install llvm"
        brew install llvm
    else
        echo "No supported package manager found." >&2
        echo "Install Clang manually: https://releases.llvm.org/" >&2
        return 1
    fi
}

if [ "$BUILD" = "1" ]; then
    if ! have_clang; then
        echo "Clang was not found."
        echo "akt needs Clang (LLVM) to compile programs."
        printf "Install it with your package manager? [y/N]: "
        read ans
        case "$ans" in
            y|Y) install_clang || exit 1 ;;
            *) echo "Skipping. Install Clang later: https://releases.llvm.org/" ;;
        esac
    fi

    if ! command -v xmake >/dev/null 2>&1; then
        echo "xmake not found." >&2
        echo "Install it: https://xmake.io/#/guide/installation" >&2
        exit 1
    fi
    if ! have_clang && ! command -v cc >/dev/null 2>&1; then
        echo "No C compiler found. Install Clang first." >&2
        exit 1
    fi

    echo "==> building akt"
    xmake f -y -m release
    xmake build
fi

if [ ! -x bin/aktc ]; then
    echo "bin/aktc not found; build failed?" >&2
    exit 1
fi

echo "==> installing to $PREFIX/akt"
mkdir -p "$PREFIX/akt/bin" "$PREFIX/akt/runtime/include" "$PREFIX/akt/runtime/src" "$PREFIX/bin"
cp -f bin/aktc bin/katek "$PREFIX/akt/bin/"
cp -f runtime/include/akt_runtime.h "$PREFIX/akt/runtime/include/"
cp -f runtime/src/akt_runtime.c "$PREFIX/akt/runtime/src/"
ln -sf "$PREFIX/akt/bin/aktc" "$PREFIX/bin/aktc"
ln -sf "$PREFIX/akt/bin/katek" "$PREFIX/bin/katek"

echo
echo "akt installed."
echo "Add this to your shell profile if needed:"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
echo
echo "Verify:"
echo "  aktc --version"
echo "  katek new hello && cd hello && katek run"
