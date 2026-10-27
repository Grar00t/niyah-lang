#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
NIYAHC=${NIYAHC:-./niyahc}
NIYAHRUN=${NIYAHRUN:-./niyahrun}
NATIVE_RESULT_PROBE=${NATIVE_RESULT_PROBE:-./tests/native_result_probe}
cases=0
failures=0
native_ok() {
    name=$1 want=$2 source=$3
    cases=$((cases + 1))
    src="$tmp/case-$cases.nya"
    exe="$tmp/case-$cases.elf"
    printf '%s\n' "$source" > "$src"
    if ! timeout 10 "$NIYAHRUN" "$src" > "$tmp/oracle" 2> "$tmp/err"; then
        echo "FAIL oracle execution $name" >&2
        cat "$tmp/oracle" "$tmp/err" >&2
        exit 1
    fi
    if test "$(cat "$tmp/oracle")" != "RESULT=$want" || test -s "$tmp/err"; then
        echo "FAIL oracle $name: expected RESULT=$want" >&2
        cat "$tmp/oracle" "$tmp/err" >&2
        exit 1
    fi
    if ! "$NIYAHC" -o "$exe" "$src" > "$tmp/compiler-out" 2> "$tmp/compiler-err"; then
        echo "FAIL compiler execution $name" >&2
        cat "$tmp/compiler-out" "$tmp/compiler-err" >&2
        exit 1
    fi
    test -x "$exe"
    got=0
    timeout --preserve-status -k 1 10 "$exe" > "$tmp/out" 2> "$tmp/err" || got=$?
    expected=$(( (want % 256 + 256) % 256 ))
    if test "$got" -ne "$expected" || test -s "$tmp/out" || test -s "$tmp/err"; then
        echo "FAIL native $name: exit=$got expected=$expected oracle=$want" >&2
        cat "$tmp/out" "$tmp/err" >&2
        exit 1
    fi
    # Observe the unmodified executable's full exit argument before the kernel
    # truncates it to an 8-bit process status. This also catches high-bit errors
    # in the entry bridge after main returns.
    if ! timeout 10 "$NATIVE_RESULT_PROBE" "$exe" > "$tmp/full-result" 2> "$tmp/full-error"; then
        echo "FAIL native full-i64 observation $name" >&2
        cat "$tmp/full-result" "$tmp/full-error" >&2
        exit 1
    fi
    if test "$(cat "$tmp/full-result")" != "RESULT=$want" || test -s "$tmp/full-error"; then
        echo "FAIL native full i64 $name: expected RESULT=$want" >&2
        cat "$tmp/full-result" "$tmp/full-error" >&2
        exit 1
    fi
}
native_bad() {
    name=$1 want=$2 source=$3
    failures=$((failures + 1))
    src="$tmp/error-$failures.nya"
    exe="$tmp/error-$failures.elf"
    printf '%s\n' "$source" > "$src"
    oracle_code=0
    timeout 10 "$NIYAHRUN" "$src" > "$tmp/oracle" 2> "$tmp/oracle-error" || oracle_code=$?
    test "$oracle_code" -eq 3
    test ! -s "$tmp/oracle"
    case "$want" in
        120) diagnostic='signed arithmetic overflow'; oracle_message='integer overflow' ;;
        121) diagnostic='division by zero'; oracle_message='(division|modulo) by zero' ;;
        122) diagnostic='shift count outside 0..63'; oracle_message='invalid shift count' ;;
        123) diagnostic='execution budget exhausted'; oracle_message='execution step limit exceeded' ;;
        124) diagnostic='call depth exceeded'; oracle_message='call depth exceeded' ;;
        125) diagnostic='uninitialized variable'; oracle_message='uninitialized variable' ;;
        *) echo "FAIL unknown trap expectation $want" >&2; exit 1 ;;
    esac
    if test "$(wc -l < "$tmp/oracle-error")" -ne 1 ||
       ! grep -Eq "^evaluation: [0-9]+:[0-9]+ $oracle_message$" "$tmp/oracle-error"; then
        echo "FAIL oracle $name diagnostic: expected $oracle_message" >&2
        cat "$tmp/oracle-error" >&2
        exit 1
    fi
    if ! "$NIYAHC" -o "$exe" "$src" > "$tmp/compiler-out" 2> "$tmp/compiler-err"; then
        echo "FAIL compiler execution $name" >&2
        cat "$tmp/compiler-out" "$tmp/compiler-err" >&2
        exit 1
    fi
    got=0
    timeout --preserve-status -k 1 10 "$exe" > "$tmp/out" 2> "$tmp/err" || got=$?
    if test "$got" -ne "$want" || test -s "$tmp/out" || test ! -s "$tmp/err"; then
        echo "FAIL native $name: exit=$got expected=$want with diagnostic" >&2
        cat "$tmp/out" "$tmp/err" >&2
        exit 1
    fi
    printf 'runtime: %s\n' "$diagnostic" > "$tmp/expected-error"
    if ! cmp -s "$tmp/expected-error" "$tmp/err"; then
        echo "FAIL native $name diagnostic: expected runtime: $diagnostic" >&2
        cat "$tmp/err" >&2
        exit 1
    fi
    # Prove normal exit, not a signal whose shell status happens to collide.
    if ! timeout 10 "$NATIVE_RESULT_PROBE" "$exe" > "$tmp/trap-result" 2> "$tmp/trap-error" ||
       test "$(cat "$tmp/trap-result")" != "RESULT=$want" ||
       ! cmp -s "$tmp/expected-error" "$tmp/trap-error"; then
        echo "FAIL native $name trap observation" >&2
        cat "$tmp/trap-result" "$tmp/trap-error" >&2
        exit 1
    fi
}
compile_bad() {
    name=$1 want=$2 source=$3
    printf '%s\n' "$source" > "$tmp/invalid.nya"
    got=0
    "$NIYAHC" -o "$tmp/must-not-exist" "$tmp/invalid.nya" > "$tmp/out" 2> "$tmp/err" || got=$?
    if test "$got" -ne "$want" || test -e "$tmp/must-not-exist" || test ! -s "$tmp/err"; then
        echo "FAIL compile $name: exit=$got expected=$want without artifact" >&2
        cat "$tmp/out" "$tmp/err" >&2
        exit 1
    fi
}
native_ok 'precedence' 14 'دالة رئيسية() -> صحيح { أرجع ٢ + ٣ * ٤؛ }'
native_ok 'parentheses' 20 'دالة رئيسية() -> صحيح { أرجع (٢ + ٣) * ٤؛ }'
native_ok 'negative division' -2 'دالة رئيسية() -> صحيح { أرجع -٧ / ٣؛ }'
native_ok 'negative remainder' -1 'دالة رئيسية() -> صحيح { أرجع -٧ % ٣؛ }'
native_ok 'bitwise' 11 'دالة رئيسية() -> صحيح { أرجع (٦ & ٣) | (٨ ^ ١)؛ }'
native_ok 'bitwise not' -2 'دالة رئيسية() -> صحيح { أرجع ~١؛ }'
native_ok 'left shift' 48 'دالة رئيسية() -> صحيح { أرجع ٣ << ٤؛ }'
native_ok 'arithmetic right shift' -4 'دالة رئيسية() -> صحيح { أرجع -١٦ >> ٢؛ }'
native_ok 'right shift 63' -1 'دالة رئيسية() -> صحيح { أرجع -١ >> ٦٣؛ }'
native_ok 'zero left shift 63' 0 'دالة رئيسية() -> صحيح { أرجع ٠ << ٦٣؛ }'
native_ok 'negative left shift boundary' -9223372036854775808 'دالة رئيسية() -> صحيح { أرجع -١ << ٦٣؛ }'
native_ok 'signed minimum' -9223372036854775808 'دالة رئيسية() -> صحيح { أرجع -9223372036854775807 - ١؛ }'
native_ok 'signed maximum' 9223372036854775807 'دالة رئيسية() -> صحيح { أرجع 9223372036854775807؛ }'
native_ok 'local assignment' 9 'دالة رئيسية() -> صحيح { متغير س: صحيح؛ س = ٧؛ س = س + ٢؛ أرجع س؛ }'
native_ok 'nested lexical shadow' 5 'دالة رئيسية() -> صحيح { متغير س: صحيح = ٥؛ { متغير س: صحيح = ٩؛ س = ١؛ } أرجع س؛ }'
native_ok 'while sum' 55 'دالة رئيسية() -> صحيح { متغير س: صحيح = ١؛ متغير ج: صحيح = ٠؛ طالما (س <= ١٠) { ج = ج + س؛ س = س + ١؛ } أرجع ج؛ }'
native_ok 'loop local lifetime' 6 'دالة رئيسية() -> صحيح { متغير س: صحيح = ٠؛ متغير ج: صحيح = ٠؛ طالما (س < ٣) { متغير ق: صحيح = ٢؛ ج = ج + ق؛ س = س + ١؛ } أرجع ج؛ }'
native_ok 'nested loops' 12 'دالة رئيسية() -> صحيح { متغير س: صحيح = ٠؛ متغير ج: صحيح = ٠؛ طالما (س < ٣) { متغير ص: صحيح = ٠؛ طالما (ص < ٤) { ج = ج + ١؛ ص = ص + ١؛ } س = س + ١؛ } أرجع ج؛ }'
native_ok 'else if' 7 'دالة رئيسية() -> صحيح { متغير س: صحيح = ٢؛ إذا (س == ١) { أرجع ١؛ } وإلا إذا (س == ٢) { أرجع ٧؛ } وإلا { أرجع ٣؛ } }'
native_ok 'return through loop' 3 'دالة رئيسية() -> صحيح { متغير س: صحيح = ٠؛ طالما (س < ٥) { إذا (س == ٣) { أرجع س؛ } س = س + ١؛ } أرجع -١؛ }'
native_ok 'forward function' 9 'دالة رئيسية() -> صحيح { أرجع زد(٨)؛ } دالة زد(س: صحيح) -> صحيح { أرجع س + ١؛ }'
native_ok 'recursive factorial' 720 'دالة رئيسية() -> صحيح { أرجع مضروب(٦)؛ } دالة مضروب(س: صحيح) -> صحيح { إذا (س <= ١) { أرجع ١؛ } أرجع س * مضروب(س - ١)؛ }'
native_ok 'mutual recursion' 1 'دالة زوج(س: صحيح) -> منطقي { إذا (س == ٠) { أرجع ١ == ١؛ } أرجع فرد(س - ١)؛ } دالة فرد(س: صحيح) -> منطقي { إذا (س == ٠) { أرجع ١ == ٠؛ } أرجع زوج(س - ١)؛ } دالة رئيسية() -> صحيح { إذا (زوج(١٠)) { أرجع ١؛ } وإلا { أرجع ٠؛ } }'
native_ok 'eight arguments' 36 'دالة جمع(أ: صحيح، ب: صحيح، ج: صحيح، د: صحيح، ه: صحيح، و: صحيح، ز: صحيح، ح: صحيح) -> صحيح { أرجع أ + ب + ج + د + ه + و + ز + ح؛ } دالة رئيسية() -> صحيح { أرجع جمع(١،٢،٣،٤،٥،٦،٧،٨)؛ }'
native_ok 'byte call' 1 'متغير ب: بايت؛ دالة هوية(س: بايت) -> بايت { أرجع س؛ } دالة رئيسية() -> صحيح { إذا (هوية(ب) == ب) { أرجع ١؛ } وإلا { أرجع ٠؛ } }'
native_ok 'boolean call' 1 'دالة نفي(س: منطقي) -> منطقي { أرجع !س؛ } دالة رئيسية() -> صحيح { إذا (نفي(١ > ٢)) { أرجع ١؛ } وإلا { أرجع ٠؛ } }'
native_ok 'void function effect' 5 'متغير س: صحيح = ١؛ دالة زد() { س = س + ٤؛ } دالة رئيسية() -> صحيح { زد()؛ أرجع س؛ }'
native_ok 'global call initializer' 9 'متغير س: صحيح = عدد()؛ دالة عدد() -> صحيح { أرجع ٩؛ } دالة رئيسية() -> صحيح { أرجع س؛ }'
native_ok 'default global initialization' 0 'متغير س: صحيح؛ دالة رئيسية() -> صحيح { أرجع س؛ }'
native_ok 'left to right arguments' 12 'متغير س: صحيح = ٠؛ دالة زد() -> صحيح { س = س + ١؛ أرجع س؛ } دالة جمع(أ: صحيح، ب: صحيح) -> صحيح { أرجع أ * ١٠ + ب؛ } دالة رئيسية() -> صحيح { أرجع جمع(زد()، زد())؛ }'
native_ok 'short circuit and' 3 'دالة رئيسية() -> صحيح { إذا ((١ == ٠) && (١ / ٠ > ٠)) { أرجع ٩؛ } وإلا { أرجع ٣؛ } }'
native_ok 'short circuit or' 4 'دالة رئيسية() -> صحيح { إذا ((١ == ١) || (١ / ٠ > ٠)) { أرجع ٤؛ } وإلا { أرجع ٩؛ } }'
native_ok 'short circuit skips effects' 0 'متغير س: صحيح = ٠؛ دالة أثر() -> منطقي { س = س + ١؛ أرجع ١ == ١؛ } دالة رئيسية() -> صحيح { متغير ب: منطقي = (١ == ٠) && أثر()؛ ب = (١ == ١) || أثر()؛ أرجع س؛ }'
native_ok 'parameter shadow' 7 'دالة د(س: صحيح) -> صحيح { { متغير س: صحيح = ٩؛ } أرجع س؛ } دالة رئيسية() -> صحيح { أرجع د(٧)؛ }'
native_ok 'block assignment outer' 8 'دالة رئيسية() -> صحيح { متغير س: صحيح = ١؛ { س = ٨؛ } أرجع س؛ }'
native_ok 'negative exit domain' -1 'دالة رئيسية() -> صحيح { أرجع -١؛ }'
native_ok 'comparison operators' 6 'دالة رئيسية() -> صحيح { إذا ((١ < ٢) && (٢ <= ٢) && (٣ > ٢) && (٣ >= ٣) && (٢ != ١) && (١ == ١)) { أرجع ٦؛ } وإلا { أرجع ٠؛ } }'
native_ok 'global initialized by earlier call' 5 'متغير أ: صحيح = جهز()؛ متغير ب: صحيح؛ دالة جهز() -> صحيح { ب = ٥؛ أرجع ب؛ } دالة رئيسية() -> صحيح { أرجع أ + ب؛ }'
native_ok 'later declaration replaces assigned global' 94 'متغير أ: صحيح = جهز()؛ متغير ب: صحيح = ٤؛ دالة جهز() -> صحيح { ب = ٩؛ أرجع ب؛ } دالة رئيسية() -> صحيح { أرجع أ * ١٠ + ب؛ }'
native_ok 'else block fuel parity' 300000 'دالة رئيسية() -> صحيح { متغير س: صحيح = ٠؛ طالما (س < 300000) { إذا (١ < ٠) {} وإلا {} س = س + ١؛ } أرجع س؛ }'
parameters=''
arguments=''
i=1
while test "$i" -le 64; do
    if test "$i" -gt 1; then parameters="$parameters،"; arguments="$arguments،"; fi
    parameters="${parameters}p$i: صحيح"
    arguments="$arguments$i"
    i=$((i + 1))
done
native_ok '64 argument boundary' 65 "دالة جمع($parameters) -> صحيح { أرجع p1 + p64؛ } دالة رئيسية() -> صحيح { أرجع جمع($arguments)؛ }"
locals=''
i=0
while test "$i" -lt 130; do
    locals="$locals متغير v$i: صحيح = ٠؛"
    i=$((i + 1))
done
native_ok 'recursive local capacity' 126 "دالة د(n: صحيح) -> صحيح { $locals إذا (n > ٠) { أرجع ١ + د(n - ١)؛ } أرجع ٠؛ } دالة رئيسية() -> صحيح { أرجع د(126)؛ }"
i=1
while test "$i" -le 50; do
    expected=$(( (i * 7 - 3) / 2 + i % 5 ))
    native_ok "arithmetic sample $i" "$expected" "دالة رئيسية() -> صحيح { متغير س: صحيح = $i؛ أرجع (س * ٧ - ٣) / ٢ + س % ٥؛ }"
    i=$((i + 1))
done
# Layout must not add surrounding statement depth to expression depth.
expression='١'
i=0
while test "$i" -lt 127; do expression="$expression + ١"; i=$((i + 1)); done
native_ok '127-operator expression' 128 "دالة رئيسية() -> صحيح { أرجع $expression؛ }"
expression="$expression + ١"
native_ok '128-operator expression' 129 "دالة رئيسية() -> صحيح { أرجع $expression؛ }"
native_ok 'nested blocks with deep expression' 129 "دالة رئيسية() -> صحيح { {{{{{{{{{{ أرجع $expression؛ }}}}}}}}}} }"

# Full-width values and branches whose low byte alone is insufficient.
native_ok 'low byte collision one' 1 'دالة رئيسية() -> صحيح { أرجع ١؛ }'
native_ok 'low byte collision 257' 257 'دالة رئيسية() -> صحيح { أرجع ٢٥٧؛ }'
native_ok 'low byte collision 65537' 65537 'دالة رئيسية() -> صحيح { أرجع 65537؛ }'
native_ok 'low byte collision high32' 4294967297 'دالة رئيسية() -> صحيح { أرجع 4294967297؛ }'
native_ok 'low byte collision high40' 1099511627777 'دالة رئيسية() -> صحيح { أرجع 1099511627777؛ }'
native_ok 'negative low byte collision' -255 'دالة رئيسية() -> صحيح { أرجع -٢٥٥؛ }'
native_ok 'negative high32 collision' -4294967295 'دالة رئيسية() -> صحيح { أرجع -4294967295؛ }'
native_ok 'high multiply' 4503599627370496 'دالة رئيسية() -> صحيح { أرجع 4294967296 * 1048576؛ }'
native_ok 'high divide' 4294967296 'دالة رئيسية() -> صحيح { أرجع 4611686018427387904 / 1073741824؛ }'
native_ok 'negative high divide' -4294967296 'دالة رئيسية() -> صحيح { أرجع -4611686018427387904 / 1073741824؛ }'
native_ok 'high remainder' 1099511627775 'دالة رئيسية() -> صحيح { أرجع 9223372036854775807 % 1099511627776؛ }'
native_ok 'left shift high32' 2147483648 'دالة رئيسية() -> صحيح { أرجع ١ << ٣١؛ }'
native_ok 'left shift high62' 4611686018427387904 'دالة رئيسية() -> صحيح { أرجع ١ << ٦٢؛ }'
native_ok 'negative left shift high62' -9223372036854775808 'دالة رئيسية() -> صحيح { أرجع -٢ << ٦٢؛ }'
native_ok 'negative right shift high40' -4294967296 'دالة رئيسية() -> صحيح { أرجع -1099511627776 >> ٨؛ }'
native_ok 'high bitwise and' 1099511627776 'دالة رئيسية() -> صحيح { أرجع 1099511627777 & 1099511627776؛ }'
native_ok 'high bitwise or' 1099511628032 'دالة رئيسية() -> صحيح { أرجع 1099511627776 | ٢٥٦؛ }'
native_ok 'high bitwise xor' 1099511627776 'دالة رئيسية() -> صحيح { أرجع 1099511627777 ^ ١؛ }'
native_ok 'high bitwise not' -4294967297 'دالة رئيسية() -> صحيح { أرجع ~4294967296؛ }'
native_ok 'high carry' 4294967296 'دالة رئيسية() -> صحيح { أرجع 4294967295 + ١؛ }'
native_ok 'high borrow' 4294967295 'دالة رئيسية() -> صحيح { أرجع 4294967296 - ١؛ }'
native_ok 'high bits equality branch' 0 'دالة رئيسية() -> صحيح { إذا (١ == ٢٥٧) { أرجع ١؛ } وإلا { أرجع ٠؛ } }'
native_ok 'high signed comparison' 1 'دالة رئيسية() -> صحيح { إذا ((2147483648 > ١) && (-4294967295 < ١) && (4294967297 != ١)) { أرجع ١؛ } وإلا { أرجع ٠؛ } }'
native_ok 'high argument and return' 1099511627777 'دالة هوية(س: صحيح) -> صحيح { أرجع س؛ } دالة رئيسية() -> صحيح { أرجع هوية(1099511627777)؛ }'
# The literal RHS optimization must preserve effects and exact fuel use.
native_ok 'literal RHS after effectful left call' 2147483649 'متغير ج: صحيح = ٠؛ دالة أثر() -> صحيح { ج = ج + ١؛ أرجع 4294967296؛ } دالة رئيسية() -> صحيح { متغير ن: صحيح = أثر() - 2147483648؛ أرجع ن + ج؛ }'
native_ok 'mixed paths preserve binary call order' 208 'متغير ج: صحيح = ٠؛ دالة أثر() -> صحيح { ج = ج + ١؛ أرجع ج؛ } دالة رئيسية() -> صحيح { متغير ن: صحيح = (أثر() + ١) * (أثر() + ٢)؛ أرجع ن + ج * ١٠٠؛ }'
# 1 declaration + 1 while statement + (N+1) conditions + N assignments + 1 return.
native_ok 'literal loop exact fuel boundary' 499998 'دالة رئيسية() -> صحيح { متغير س: صحيح = ٠؛ طالما (س < 499998) { س = س + ١؛ } أرجع س؛ }'
native_bad literal_loop_fuel_exhaustion 123 'دالة رئيسية() -> صحيح { متغير س: صحيح = ٠؛ طالما (س < 499999) { س = س + ١؛ } أرجع س؛ }'
native_bad addition_overflow 120 'دالة رئيسية() -> صحيح { أرجع 9223372036854775807 + ١؛ }'
native_bad subtraction_overflow 120 'دالة رئيسية() -> صحيح { أرجع (-9223372036854775807 - ١) - ١؛ }'
native_bad multiplication_overflow 120 'دالة رئيسية() -> صحيح { أرجع 9223372036854775807 * ٢؛ }'
native_bad negate_overflow 120 'دالة رئيسية() -> صحيح { أرجع -(-9223372036854775807 - ١)؛ }'
native_bad divide_overflow 120 'دالة رئيسية() -> صحيح { أرجع (-9223372036854775807 - ١) / -١؛ }'
native_bad modulo_overflow 120 'دالة رئيسية() -> صحيح { أرجع (-9223372036854775807 - ١) % -١؛ }'
native_bad divide_zero 121 'دالة رئيسية() -> صحيح { أرجع ١ / ٠؛ }'
native_bad modulo_zero 121 'دالة رئيسية() -> صحيح { أرجع ١ % ٠؛ }'
native_bad shift_negative 122 'دالة رئيسية() -> صحيح { أرجع ١ << -١؛ }'
native_bad shift_wide 122 'دالة رئيسية() -> صحيح { أرجع ١ >> ٦٤؛ }'
native_bad shift_overflow 120 'دالة رئيسية() -> صحيح { أرجع ١ << ٦٣؛ }'
native_bad uninitialized_local 125 'دالة رئيسية() -> صحيح { متغير س: صحيح؛ أرجع س؛ }'
native_bad uninitialized_shadow 125 'دالة رئيسية() -> صحيح { متغير س: صحيح = ٩؛ { متغير س: صحيح؛ أرجع س؛ } }'
native_bad uninitialized_branch 125 'دالة رئيسية() -> صحيح { متغير س: صحيح؛ إذا (١ == ٠) { س = ١؛ } أرجع س؛ }'
native_bad uninitialized_global 125 'متغير س: صحيح = اقرأ()؛ متغير ص: صحيح = ٩؛ دالة اقرأ() -> صحيح { أرجع ص؛ } دالة رئيسية() -> صحيح { أرجع س؛ }'
native_bad runaway_loop 123 'دالة رئيسية() -> صحيح { طالما (١ == ١) {} أرجع ٠؛ }'
native_bad runaway_recursion 124 'دالة د() -> صحيح { أرجع د()؛ } دالة رئيسية() -> صحيح { أرجع د()؛ }'
negations=''
i=0
while test "$i" -lt 110; do negations="$negations-"; i=$((i + 1)); done
native_bad deep_expression_recursive_guard 124 "دالة د() -> صحيح { أرجع ${negations}د()؛ } دالة رئيسية() -> صحيح { أرجع د()؛ }"
compile_bad syntax 2 'دالة رئيسية() -> صحيح { أرجع ١ + ؛ }'
compile_bad type 3 'دالة رئيسية() -> صحيح { متغير س: منطقي = ١؛ أرجع ٠؛ }'
compile_bad pointer 3 'دالة رئيسية() -> صحيح { متغير س: *صحيح؛ أرجع ٠؛ }'
compile_bad array 3 'دالة رئيسية() -> صحيح { متغير س: [٢]صحيح؛ أرجع ٠؛ }'
compile_bad missing_main 4 'دالة أخرى() -> صحيح { أرجع ٠؛ }'
compile_bad main_parameters 4 'دالة رئيسية(س: صحيح) -> صحيح { أرجع س؛ }'
compile_bad main_result 4 'دالة رئيسية() -> منطقي { أرجع ١ == ١؛ }'
# A successful result is an actual static ELF64 file with no dynamic loader.
readelf -h "$tmp/case-1.elf" > "$tmp/header"
grep -Fq 'ELF64' "$tmp/header"
grep -Fq 'EXEC' "$tmp/header"
readelf -l "$tmp/case-1.elf" > "$tmp/segments"
grep -Fq 'LOAD' "$tmp/segments"
if grep -Fq 'INTERP' "$tmp/segments"; then echo 'FAIL dynamic loader dependency' >&2; exit 1; fi
# Exact recompilation must not depend on destination path or wall-clock time.
"$NIYAHC" "$tmp/case-1.nya" -o "$tmp/repeat.elf"
cmp "$tmp/case-1.elf" "$tmp/repeat.elf"
# Publishing must preserve existing output files, symlinks and directories.
printf 'preserve-this-output\n' > "$tmp/existing"
cp "$tmp/existing" "$tmp/sentinel"
status=0
"$NIYAHC" -o "$tmp/existing" "$tmp/case-1.nya" > "$tmp/out" 2> "$tmp/err" || status=$?
test "$status" -eq 1
cmp "$tmp/existing" "$tmp/sentinel"
ln -s "$tmp/existing" "$tmp/output-link"
status=0
"$NIYAHC" -o "$tmp/output-link" "$tmp/case-1.nya" > "$tmp/out" 2> "$tmp/err" || status=$?
test "$status" -eq 1
test -L "$tmp/output-link"
cmp "$tmp/existing" "$tmp/sentinel"
mkdir "$tmp/output-dir"
status=0
"$NIYAHC" -o "$tmp/output-dir" "$tmp/case-1.nya" > "$tmp/out" 2> "$tmp/err" || status=$?
test "$status" -eq 1
test -d "$tmp/output-dir"
status=0
"$NIYAHC" -o "$tmp/missing-parent/out" "$tmp/case-1.nya" > "$tmp/out" 2> "$tmp/err" || status=$?
test "$status" -eq 1
test ! -e "$tmp/missing-parent/out"
# Test default output and the explicit option terminator in an isolated cwd.
case "$NIYAHC" in /*) compiler=$NIYAHC;; *) compiler="$(pwd)/$NIYAHC";; esac
(cd "$tmp" && "$compiler" -- "$tmp/case-1.nya")
cmp "$tmp/case-1.elf" "$tmp/a.out"
echo "NATIVE_CLI_TEST_PASS: $cases full-i64 native/oracle comparisons plus exit-status checks, $failures guarded runtime failures, semantic/IO/no-overwrite/ELF checks"
