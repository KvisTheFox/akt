#!/bin/sh
# Language and negative tests for Linux / macOS.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
AKTC="$ROOT/bin/aktc"
cd "$ROOT"

if [ ! -x "$AKTC" ]; then
    echo "aktc not found at $AKTC (build first)" >&2
    exit 1
fi

failed=0
total=0

for f in tests/lang/*.akt; do
    [ -e "$f" ] || continue
    name=$(basename "$f" .akt)
    total=$((total + 1))
    out=$("$AKTC" run "$f" 2>&1)
    rc=$?
    expected="tests/lang/$name.out"
    if [ $rc -ne 0 ]; then
        echo "FAIL $name (exit $rc)"
        printf '%s\n' "$out" | sed 's/^/    /'
        failed=$((failed + 1))
    elif [ -f "$expected" ] && [ "$out" != "$(cat "$expected")" ]; then
        echo "FAIL $name (output mismatch)"
        printf '%s\n' "$out" | sed 's/^/    /'
        failed=$((failed + 1))
    else
        echo "ok   $name"
    fi
done

for f in tests/negative/*.akt; do
    [ -e "$f" ] || continue
    name=$(basename "$f" .akt)
    total=$((total + 1))
    "$AKTC" check "$f" >/dev/null 2>&1
    rc=$?
    if [ $rc -eq 0 ]; then
        echo "FAIL $name (was accepted)"
        failed=$((failed + 1))
    else
        echo "ok   $name (rejected)"
    fi
done

echo
if [ $failed -eq 0 ]; then
    echo "all $total tests passed"
else
    echo "$failed of $total tests failed"
fi
exit $([ $failed -eq 0 ] && echo 0 || echo 1)
