#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT HUP INT TERM

./niyahparse examples/مرحبا.نيّة > "$t/summary"
grep -Eq '^OK M1 nodes=[0-9]+$' "$t/summary"
./niyahparse --ast examples/مرحبا.نيّة > "$t/ast"
grep -Fq 'PROGRAM' "$t/ast"
grep -Fq 'FUNC @2:' "$t/ast"
grep -Fq 'IF @' "$t/ast"
grep -Fq 'WHILE @' "$t/ast"
grep -Fq 'STRUCT @' "$t/ast"
./niyahparse --ast examples/مرحبا.نيّة > "$t/ast2"
cmp "$t/ast" "$t/ast2"
printf 'دالة س() { أرجع ١ + ؛ }\n' > "$t/bad.نيّة"
if ./niyahparse "$t/bad.نيّة" > "$t/bad.out"; then
    echo 'FAIL: missing operand accepted' >&2; exit 1
fi
grep -Fq 'error:' "$t/bad.out"
if grep -q '^OK ' "$t/bad.out"; then
    echo 'FAIL: parser reported success after syntax error' >&2; exit 1
fi
printf 'ثابت س: صحيح؛\n' > "$t/const.نيّة"
if ./niyahparse "$t/const.نيّة" > "$t/const.out"; then
    echo 'FAIL: constant without initializer accepted' >&2; exit 1
fi
grep -Fq 'requires initializer' "$t/const.out"
printf '\355\240\200' > "$t/malformed"
if ./niyahparse "$t/malformed" > "$t/malformed.out"; then
    echo 'FAIL: malformed UTF-8 accepted' >&2; exit 1
fi
grep -Fq 'lexical error' "$t/malformed.out"
if ./niyahparse --ast > "$t/missing"; then
    echo 'FAIL: missing source accepted' >&2; exit 1
fi
if ./niyahparse "$t/not-found.نيّة" > "$t/notfound"; then
    echo 'FAIL: non-existent file accepted' >&2; exit 1
fi
dd if=/dev/zero bs=1048576 count=4 of="$t/big" 2>/dev/null
printf 'Z' >> "$t/big"
if ./niyahparse "$t/big" > "$t/big.out"; then
    echo 'FAIL: oversize accepted' >&2; exit 1
fi
grep -Fq 'input too large' "$t/big.out"
if test -c /dev/full; then
    if ./niyahparse examples/مرحبا.نيّة > /dev/full; then
        echo 'FAIL: failed output write accepted' >&2; exit 1
    fi
fi
echo 'parser_cli_test: static syntax checker, AST, invalid input, IO bounds PASS'
