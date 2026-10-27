#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
printf 'دالة رئيسية() -> صحيح { أرجع ٢ + ٣؛ }\n' > "$tmp/five.نيه"
printf 'دالة رئيسية() -> صحيح { أرجع (٧ * ٢) - ٤؛ }\n' > "$tmp/ten.نيه"
printf 'دالة رئيسية() -> صحيح { أرجع ١ / ٠؛ }\n' > "$tmp/zero.نيه"
printf 'دالة رئيسية() -> صحيح { أرجع 9223372036854775807 + ١؛ }\n' > "$tmp/overflow.نيه"
printf 'دالة رئيسية() -> صحيح { متغير س: صحيح = ٢؛ أرجع س؛ }\n' > "$tmp/unsupported.نيه"
test "$(./niyahrun "$tmp/five.نيه")" = 'RESULT=5'
test "$(./niyahrun "$tmp/ten.نيه")" = 'RESULT=10'
if ./niyahrun "$tmp/zero.نيه" > "$tmp/out" 2>&1; then echo 'FAIL zero'; exit 1; fi
grep -Fq 'division by zero' "$tmp/out"
if ./niyahrun "$tmp/overflow.نيه" > "$tmp/out" 2>&1; then echo 'FAIL overflow'; exit 1; fi
grep -Fq 'integer overflow' "$tmp/out"
if ./niyahrun "$tmp/unsupported.نيه" > "$tmp/out" 2>&1; then echo 'FAIL unsupported'; exit 1; fi
grep -Fq 'only one return statement' "$tmp/out"
if test -c /dev/full; then
    status=0
    ./niyahrun "$tmp/five.نيه" > /dev/full 2> "$tmp/output-error" || status=$?
    if test "$status" -ne 4; then
        echo "FAIL: stdout write error exit code: $status (expected 4)" >&2
        exit 1
    fi
    grep -Fq 'failed to write result' "$tmp/output-error"
fi
echo 'RUN_CLI_TEST_PASS: results, division, overflow, unsupported syntax, stdout write failures'
