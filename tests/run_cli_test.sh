#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
NIYAHRUN=${NIYAHRUN:-./niyahrun}
cases=0
run_ok() {
    name=$1 want=$2 source=$3
    printf '%s\n' "$source" > "$tmp/input.nya"
    if ! timeout 10 "$NIYAHRUN" "$tmp/input.nya" > "$tmp/out" 2> "$tmp/err"; then
        echo "FAIL runtime execution $name" >&2
        cat "$tmp/out" "$tmp/err" >&2
        exit 1
    fi
    if test "$(cat "$tmp/out")" != "RESULT=$want" || test -s "$tmp/err"; then
        echo "FAIL runtime $name: wanted RESULT=$want" >&2
        cat "$tmp/out" "$tmp/err" >&2
        exit 1
    fi
    cases=$((cases + 1))
}
run_bad() {
    name=$1 want=$2 message=$3 source=$4
    printf '%s\n' "$source" > "$tmp/input.nya"
    got=0
    timeout 10 "$NIYAHRUN" "$tmp/input.nya" > "$tmp/out" 2> "$tmp/err" || got=$?
    if test "$got" -ne "$want" || ! grep -Fq "$message" "$tmp/err" || test -s "$tmp/out"; then
        echo "FAIL runtime $name: exit=$got wanted=$want diagnostic=$message" >&2
        cat "$tmp/out" "$tmp/err" >&2
        exit 1
    fi
    cases=$((cases + 1))
}
run_ok arithmetic 5 'دالة رئيسية() -> صحيح { أرجع ٢ + ٣؛ }'
run_ok precedence 10 'دالة رئيسية() -> صحيح { أرجع (٧ * ٢) - ٤؛ }'
run_ok local 9 'دالة رئيسية() -> صحيح { متغير س: صحيح؛ س = ٩؛ أرجع س؛ }'
run_ok scope 7 'دالة رئيسية() -> صحيح { متغير س: صحيح = ٧؛ { متغير س: صحيح = ٢؛ } أرجع س؛ }'
run_ok loop 55 'دالة رئيسية() -> صحيح { متغير س: صحيح = ١؛ متغير ج: صحيح = ٠؛ طالما (س <= ١٠) { ج = ج + س؛ س = س + ١؛ } أرجع ج؛ }'
run_ok branch 3 'دالة رئيسية() -> صحيح { إذا (١ == ٠) { أرجع ١؛ } وإلا إذا (١ > ٢) { أرجع ٢؛ } وإلا { أرجع ٣؛ } }'
run_ok forward_call 8 'دالة رئيسية() -> صحيح { أرجع د(٧)؛ } دالة د(س: صحيح) -> صحيح { أرجع س + ١؛ }'
run_ok recursion 720 'دالة رئيسية() -> صحيح { أرجع م(٦)؛ } دالة م(س: صحيح) -> صحيح { إذا (س <= ١) { أرجع ١؛ } أرجع س * م(س - ١)؛ }'
run_ok globals 5 'متغير س: صحيح = ١؛ دالة زد() { س = س + ٤؛ } دالة رئيسية() -> صحيح { زد()؛ أرجع س؛ }'
run_ok short_circuit 2 'دالة رئيسية() -> صحيح { إذا ((١ == ٠) && (١ / ٠ > ٠)) { أرجع ١؛ } وإلا { أرجع ٢؛ } }'
run_ok shift -4 'دالة رئيسية() -> صحيح { أرجع -١٦ >> ٢؛ }'
run_ok signed_min -9223372036854775808 'دالة رئيسية() -> صحيح { أرجع -9223372036854775807 - ١؛ }'
run_bad division 3 'division by zero' 'دالة رئيسية() -> صحيح { أرجع ١ / ٠؛ }'
run_bad modulo 3 'modulo by zero' 'دالة رئيسية() -> صحيح { أرجع ١ % ٠؛ }'
run_bad overflow 3 'integer overflow' 'دالة رئيسية() -> صحيح { أرجع 9223372036854775807 + ١؛ }'
run_bad shift 3 'invalid shift count' 'دالة رئيسية() -> صحيح { أرجع ١ << ٦٤؛ }'
run_bad uninitialized 3 'uninitialized variable' 'دالة رئيسية() -> صحيح { متغير س: صحيح؛ أرجع س؛ }'
run_bad uninitialized_global 3 'uninitialized variable' 'متغير س: صحيح = اقرأ()؛ متغير ص: صحيح = ٩؛ دالة اقرأ() -> صحيح { أرجع ص؛ } دالة رئيسية() -> صحيح { أرجع س؛ }'
run_bad infinite_loop 3 'execution step limit exceeded' 'دالة رئيسية() -> صحيح { طالما (١ == ١) {} أرجع ٠؛ }'
run_bad infinite_recursion 3 'call depth exceeded' 'دالة د() -> صحيح { أرجع د()؛ } دالة رئيسية() -> صحيح { أرجع د()؛ }'
run_bad type_error 5 'initializer type mismatch' 'دالة رئيسية() -> صحيح { متغير س: منطقي = ١؛ أرجع ٠؛ }'
run_bad pointer 5 'pointer, array and struct types' 'دالة رئيسية() -> صحيح { متغير س: *صحيح؛ أرجع ٠؛ }'
run_bad array 5 'pointer, array and struct types' 'دالة رئيسية() -> صحيح { متغير س: [٢]صحيح؛ أرجع ٠؛ }'
run_bad syntax 2 'expected expression' 'دالة رئيسية() -> صحيح { أرجع ١ + ؛ }'
if test -c /dev/full; then
    printf 'دالة رئيسية() -> صحيح { أرجع ٥؛ }\n' > "$tmp/five.nya"
    status=0
    "$NIYAHRUN" "$tmp/five.nya" > /dev/full 2> "$tmp/output-error" || status=$?
    test "$status" -eq 4
    grep -Fq 'failed to write result' "$tmp/output-error"
fi
echo "RUN_CLI_TEST_PASS: $cases scalar executions/errors, stdout write failure"
