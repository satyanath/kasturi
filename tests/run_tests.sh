#!/bin/sh
# run_tests.sh - Kasturi test runner for Linux / macOS (same rules as run_tests.ps1)
#
#   sh tests/run_tests.sh [name-pattern]
cd "$(dirname "$0")/.." || exit 1
ROOT=$(pwd)
K="$ROOT/bin/kasturi"
WORK="$ROOT/build/tests"
PATTERN=${1:-*}
mkdir -p "$WORK"
pass=0
fail=0
failed=""

for src in tests/cases/$PATTERN.kst; do
    [ -f "$src" ] || continue
    name=$(basename "$src" .kst)
    base="$ROOT/tests/cases/$name"
    if ! "$K" -o "$WORK/$name" "$src" 2>"$WORK/$name.cc"; then
        echo "FAIL  $name (compile)"
        cat "$WORK/$name.cc"
        fail=$((fail + 1)); failed="$failed $name"
        continue
    fi
    set --
    if [ -f "$base.args" ]; then
        while IFS= read -r a || [ -n "$a" ]; do
            [ -n "$a" ] && set -- "$@" "$a"
        done <"$base.args"
    fi
    input=/dev/null
    [ -f "$base.in" ] && input="$base.in"
    out=$(cd "$WORK" && "./$name" "$@" <"$input" 2>"$WORK/$name.err")
    code=$?
    err=$(cat "$WORK/$name.err")
    if [ -n "$err" ]; then
        got=$(printf '%s\n\n[stderr]\n%s' "$out" "$err")
    else
        got=$out
    fi
    want=$(cat "$base.out" 2>/dev/null)
    want_code=0
    [ -f "$base.exit" ] && want_code=$(tr -d ' \r\n' <"$base.exit")
    if [ "$got" = "$want" ] && [ "$code" -eq "$want_code" ]; then
        echo "ok    $name"
        pass=$((pass + 1))
    else
        echo "FAIL  $name (exit $code, expected $want_code)"
        printf '%s\n' "$got" >"$WORK/$name.got"
        printf '%s\n' "$want" >"$WORK/$name.want"
        diff "$WORK/$name.want" "$WORK/$name.got" | head -n 10
        fail=$((fail + 1)); failed="$failed $name"
    fi
done

for src in tests/errors/$PATTERN.kst; do
    [ -f "$src" ] || continue
    name="errors/$(basename "$src" .kst)"
    expect=$(sed -n '1s#^//[^:]*:[[:space:]]*##p' "$src" | tr -d '\r')
    if "$K" -o "$WORK/error_case" "$src" 2>"$WORK/error.txt" >/dev/null; then
        echo "FAIL  $name (was accepted)"
        fail=$((fail + 1)); failed="$failed $name"
    elif grep -qF "$expect" "$WORK/error.txt"; then
        echo "ok    $name"
        pass=$((pass + 1))
    else
        echo "FAIL  $name; expected diagnostic containing: $expect"
        cat "$WORK/error.txt"
        fail=$((fail + 1)); failed="$failed $name"
    fi
done

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ] || { echo "failed:$failed"; exit 1; }
