#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT HUP INT TERM
check() {
    want=$1
    match=$2
    file=$3
    set +e
    ./niyahparse --check "$file" > "$t/out" 2>&1
    got=$?
    set -e
    if test "$want" -ne "$got"; then
        echo "FAIL: expected exit $want; got $got" >&2
        cat "$t/out" >&2
        exit 1
    fi
    grep -Fq "$match" "$t/out"
}
printf 'دالة رئيسية() -> صحيح { متغير الناتج: صحيح = ٢ + ٣؛ أرجع الناتج؛ }\n' > "$t/good"
printf 'دالة د(س: صحيح) -> صحيح { أرجع س + ١؛ }\n' > "$t/param"
printf 'دالة د() { ثابت س: صحيح = ١؛ س = ٢؛ }\n' > "$t/const"
printf 'دالة د() -> صحيح { أرجع مفقود؛ }\n' > "$t/unknown"
printf 'دالة د() { متغير س: صحيح؛ متغير س: صحيح؛ }\n' > "$t/dup"
printf 'دالة د() { متغير س: منطقي = ١؛ }\n' > "$t/mismatch"
printf 'دالة د() { ١ = ٢؛ }\n' > "$t/lvalue"
printf 'دالة د() { متغير س: *صحيح؛ }\n' > "$t/pointer"
printf 'دالة د() { س(١)؛ }\n' > "$t/call"
printf 'دالة د() { أرجع ١ + ؛ }\n' > "$t/syntax"
printf '\355\240\200' > "$t/invalidutf8"
check 0 'OK M2 checked_nodes=' "$t/good"
check 0 'OK M2 checked_nodes=' "$t/param"
check 4 'cannot assign to constant' "$t/const"
check 4 'undefined identifier' "$t/unknown"
check 4 'duplicate symbol in scope' "$t/dup"
check 4 'initializer type mismatch' "$t/mismatch"
check 4 'invalid assignment target' "$t/lvalue"
check 4 'pointer, array and struct types require a later M2 stage' "$t/pointer"
check 4 'expression form not implemented in M2 slice' "$t/call"
check 2 'expected expression' "$t/syntax"
check 2 'lexical error' "$t/invalidutf8"
./niyahparse "$t/const" > "$t/m1"
grep -Eq '^OK M1 nodes=[0-9]+$' "$t/m1"
./niyahparse --ast "$t/good" > "$t/ast"
grep -Fq 'PROGRAM' "$t/ast"
grep -Fq 'RETURN' "$t/ast"
set +e
./niyahparse --check > "$t/usage" 2>&1
usage_code=$?
set -e
test "$usage_code" -eq 1
grep -Fq 'usage:' "$t/usage"
if test -c /dev/full; then
    set +e
    ./niyahparse --check "$t/good" > /dev/full 2>&1
    write_code=$?
    set -e
    test "$write_code" -eq 3
fi
echo 'CHECK_CLI_TEST_PASS: 2 valid, 9 rejected, M1 compatibility'
