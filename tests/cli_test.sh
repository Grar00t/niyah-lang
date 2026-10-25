#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
printf 'والكتاب سيكتب مكتوب\n' > "$tmp/words.txt"
./niyahlex --morph "$tmp/words.txt" > "$tmp/morph.out"
grep -Fq 'CONJ_W(و) + DET_AL(ال) + STEM(كتاب)' "$tmp/morph.out"
grep -Fq 'FUTURE_S(س) + IMPF_Y(ي) + STEM(كتب)' "$tmp/morph.out"
grep -Fq 'pattern=MAFOOL' "$tmp/morph.out"
grep -Fq 'root=كتب' "$tmp/morph.out"
# ASCII Al- is an explicit transliteration convention; hyphen is intentionally
# not a generic word separator in --morph mode.
printf 'Al-book\n' > "$tmp/al-prefixed.txt"
./niyahlex --morph "$tmp/al-prefixed.txt" > "$tmp/al-prefixed.out"
grep -Fq 'DET_AL(Al-) + STEM(book)' "$tmp/al-prefixed.out"
printf 'دالة رئيسية() -> صحيح { متغير والكتاب: صحيح = 5؛ أرجع والكتاب؛ }\n' > "$tmp/code.نيّة"
./niyahlex --annotate "$tmp/code.نيّة" > "$tmp/annotated.out"
grep -Fq 'IDENT_MORPH' "$tmp/annotated.out"
grep -Fq 'CONJ_W(و) + DET_AL(ال) + STEM(كتاب)' "$tmp/annotated.out"
./niyahlex "$tmp/code.نيّة" > "$tmp/default.out"
if grep -q 'MORPH' "$tmp/default.out"; then
    echo 'FAIL: default lexer output changed' >&2; exit 1
fi
# Removing only the diagnostic lines must reproduce the unmodified tokens.
# Filter output by its actual record grammar, never by a substring of source text.
# An identifier called IDENT_MORPH is still a legitimate code token.
awk '
    /^[0-9]+:[0-9]+[[:space:]]+IDENT_MORPH[[:space:]]/ { next }
    /^  [0-9]+[[:space:]]/ { next }
    { print }
' "$tmp/annotated.out" > "$tmp/filtered.out"
cmp "$tmp/default.out" "$tmp/filtered.out"

printf 'متغير IDENT_MORPH: صحيح = ١؛\n' > "$tmp/named-after-metadata.نيّة"
./niyahlex "$tmp/named-after-metadata.نيّة" > "$tmp/expected-named.out"
./niyahlex --annotate "$tmp/named-after-metadata.نيّة" > "$tmp/annotated-named.out"
awk '
    /^[0-9]+:[0-9]+[[:space:]]+IDENT_MORPH[[:space:]]/ { next }
    /^  [0-9]+[[:space:]]/ { next }
    { print }
' "$tmp/annotated-named.out" > "$tmp/filtered-named.out"
cmp "$tmp/expected-named.out" "$tmp/filtered-named.out"
grep -Fq "$(printf '\tIDENT\tIDENT_MORPH')" "$tmp/filtered-named.out"

# 97 valid Arabic base characters exceed the morph analyzer's 96-base cap,
# but the programming identifier and annotation mode must remain valid.
printf 'متغير ' > "$tmp/long-code.نيّة"
i=0
while [ "$i" -lt 97 ]; do printf 'ك' >> "$tmp/long-code.نيّة"; i=$((i+1)); done
printf ': صحيح = ٤٢؛\n' >> "$tmp/long-code.نيّة"
./niyahlex "$tmp/long-code.نيّة" > /dev/null
./niyahlex --annotate "$tmp/long-code.نيّة" > "$tmp/long-annotated.out"
grep -Fq 'status=2' "$tmp/long-annotated.out"
grep -Fq 'truncated=1' "$tmp/long-annotated.out"
printf '\355\240\200' > "$tmp/bad-utf8"
if ./niyahlex --morph "$tmp/bad-utf8" > "$tmp/bad.out"; then
    echo 'FAIL: invalid UTF-8 should be rejected' >&2; exit 1
fi
grep -aFq 'status=1' "$tmp/bad.out"
dd if=/dev/zero bs=1048576 count=4 of="$tmp/large" 2>/dev/null
printf 'X' >> "$tmp/large"
if ./niyahlex --morph "$tmp/large" > "$tmp/large.out"; then
    echo 'FAIL: silent oversize truncation' >&2; exit 1
fi
grep -Fq 'input too large' "$tmp/large.out"
if ./niyahlex --morph > "$tmp/missing.out"; then
    echo 'FAIL: --morph without file' >&2; exit 1
fi
if ./niyahlex --annotate > "$tmp/missing2.out"; then
    echo 'FAIL: --annotate without file' >&2; exit 1
fi
# /dev/full deterministically exercises failed stdout writes on Linux.
if test -c /dev/full; then
    if ./niyahlex "$tmp/code.نيّة" > /dev/full; then
        echo 'FAIL: stdout error was silently ignored' >&2
        exit 1
    fi
fi
echo 'cli_test: morphology, source annotation, invalid UTF-8, size bound, output errors PASS'
