#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
compiler=${CC:-cc}
NIYAHC=${NIYAHC:-./niyahc}
NATIVE_RESULT_PROBE=${NATIVE_RESULT_PROBE:-./tests/native_result_probe}

# Change only the entry bridge in a temporary compiler: add 256 after main.
# This preserves the public 8-bit exit status but corrupts the observed i64.
awk '
{
    if (index($0, "raw(e,\"\\x48\\x89\\xc7\",3);byte(e,0xb8);word32(e,60);")) {
        print "    raw(e,\"\\x48\\x05\\x00\\x01\\x00\\x00\",6);"
        matches++
    }
    print
}
END { if (matches != 1) exit 1 }
' src/codegen.c > "$tmp/codegen-mutant.c"
"$compiler" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Isrc \
    -o "$tmp/niyahc-mutant" src/compile_cli.c "$tmp/codegen-mutant.c" \
    src/check.c src/parser.c src/lexer.c

printf 'دالة رئيسية() -> صحيح { أرجع ١؛ }\n' > "$tmp/one.niy"
"$NIYAHC" -o "$tmp/normal" "$tmp/one.niy"
"$tmp/niyahc-mutant" -o "$tmp/mutated" "$tmp/one.niy"
normal_status=0
mutant_status=0
timeout 10 "$tmp/normal" > "$tmp/normal-out" 2> "$tmp/normal-error" || normal_status=$?
timeout 10 "$tmp/mutated" > "$tmp/mutated-out" 2> "$tmp/mutated-error" || mutant_status=$?
test "$normal_status" -eq 1
test "$mutant_status" -eq 1
test ! -s "$tmp/normal-out"
test ! -s "$tmp/normal-error"
test ! -s "$tmp/mutated-out"
test ! -s "$tmp/mutated-error"
timeout 10 "$NATIVE_RESULT_PROBE" "$tmp/normal" > "$tmp/normal-result"
timeout 10 "$NATIVE_RESULT_PROBE" "$tmp/mutated" > "$tmp/mutated-result"
test "$(cat "$tmp/normal-result")" = 'RESULT=1'
test "$(cat "$tmp/mutated-result")" = 'RESULT=257'

# The real suite must fail on the changed full value, not an unrelated setup error.
status=0
NIYAHC="$tmp/niyahc-mutant" NATIVE_RESULT_PROBE="$NATIVE_RESULT_PROBE" \
    sh tests/native_cli_test.sh > "$tmp/suite-out" 2> "$tmp/suite-error" || status=$?
test "$status" -eq 1
grep -Fq 'FAIL native full i64 precedence: expected RESULT=14' "$tmp/suite-error"
grep -Fxq 'RESULT=270' "$tmp/suite-error"

# A failed exec must never masquerade as a successfully observed program exit.
status=0
timeout 10 "$NATIVE_RESULT_PROBE" "$tmp/does-not-exist" \
    > "$tmp/missing-out" 2> "$tmp/missing-error" || status=$?
test "$status" -ne 0
test ! -s "$tmp/missing-out"
test -s "$tmp/missing-error"
echo 'NATIVE_RESULT_MUTATION_PASS: equal 8-bit exits, distinct i64 values, corrupted entry result rejected'

# A trap with the right status and wrong diagnostic must also be rejected.
awk '
{
    if (sub(/runtime: signed arithmetic overflow/, "runtime: division by zero"))
        matches++
    print
}
END { if (matches != 1) exit 1 }
' src/codegen.c > "$tmp/codegen-wrong-diagnostic.c"
"$compiler" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Isrc \
    -o "$tmp/niyahc-wrong-diagnostic" src/compile_cli.c "$tmp/codegen-wrong-diagnostic.c" \
    src/check.c src/parser.c src/lexer.c
status=0
NIYAHC="$tmp/niyahc-wrong-diagnostic" NATIVE_RESULT_PROBE="$NATIVE_RESULT_PROBE" \
    sh tests/native_cli_test.sh > "$tmp/diagnostic-out" 2> "$tmp/diagnostic-error" || status=$?
test "$status" -eq 1
grep -Fxq 'FAIL native addition_overflow diagnostic: expected runtime: signed arithmetic overflow' "$tmp/diagnostic-error"
grep -Fxq 'runtime: division by zero' "$tmp/diagnostic-error"
echo 'NATIVE_DIAGNOSTIC_MUTATION_PASS: correct status with wrong trap diagnostic rejected'
