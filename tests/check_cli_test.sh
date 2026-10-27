#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT HUP INT TERM
valid=0
rejected=0
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
    if test "$want" -eq 0; then valid=$((valid + 1)); else rejected=$((rejected + 1)); fi
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
check 4 'undefined identifier' "$t/call"
check 2 'expected expression' "$t/syntax"
check 2 'lexical error' "$t/invalidutf8"
printf 'دالة رئيسية() -> صحيح { أرجع زد(٤)؛ } دالة زد(س: صحيح) -> صحيح { أرجع س + ١؛ }\n' > "$t/forwardcall"
printf 'دالة أ(س: صحيح) -> صحيح { أرجع ب(س)؛ } دالة ب(س: صحيح) -> صحيح { أرجع أ(س)؛ }\n' > "$t/mutualcall"
printf 'دالة رئيسية() { عمل()؛ أرجع؛ } دالة عمل() { أرجع؛ }\n' > "$t/voidstatement"
printf 'متغير س: صحيح = عدد()؛ دالة عدد() -> صحيح { أرجع ٧؛ }\n' > "$t/globalcall"
printf 'دالة د(س: صحيح) -> صحيح { أرجع س؛ } دالة رئيسية() -> صحيح { أرجع د()؛ }\n' > "$t/callarity"
printf 'دالة د(س: منطقي) -> منطقي { أرجع س؛ } دالة رئيسية() -> منطقي { أرجع د(١)؛ }\n' > "$t/calltype"
printf 'دالة د() -> صحيح { أرجع ١؛ } دالة رئيسية() -> صحيح { متغير د: صحيح = ٢؛ أرجع د()؛ }\n' > "$t/callshadow"
printf 'دالة عمل() { أرجع؛ } دالة رئيسية() -> منطقي { أرجع عمل() == عمل()؛ }\n' > "$t/voidequality"
printf 'دالة عمل() { أرجع؛ } دالة رئيسية() { أرجع عمل()؛ }\n' > "$t/voidreturn"
printf 'دالة د() -> صحيح { أرجع ١؛ } دالة رئيسية() -> صحيح { أرجع د()()؛ }\n' > "$t/nestedcallee"
check 0 'OK M2 checked_nodes=' "$t/forwardcall"
check 0 'OK M2 checked_nodes=' "$t/mutualcall"
check 0 'OK M2 checked_nodes=' "$t/voidstatement"
check 0 'OK M2 checked_nodes=' "$t/globalcall"
check 4 'argument count mismatch' "$t/callarity"
check 4 'argument type mismatch' "$t/calltype"
check 4 'call target is not a function' "$t/callshadow"
check 4 'void expression cannot be used as a value' "$t/voidequality"
check 4 'void call cannot be returned as a value' "$t/voidreturn"
check 4 'call target must be a function name' "$t/nestedcallee"
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
echo "CHECK_CLI_TEST_PASS: $valid valid, $rejected rejected, M1 compatibility and scalar calls"
