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
printf 'دالة رئيسية() -> صحيح { متغير والكتاب: صحيح = 5؛ أرجع والكتاب؛ }\n' > "$tmp/code.نيّة"
./niyahlex --annotate "$tmp/code.نيّة" > "$tmp/annotated.out"
grep -Fq 'IDENT_MORPH' "$tmp/annotated.out"
grep -Fq 'CONJ_W(و) + DET_AL(ال) + STEM(كتاب)' "$tmp/annotated.out"
./niyahlex "$tmp/code.نيّة" > "$tmp/default.out"
if grep -q 'MORPH' "$tmp/default.out"; then
    echo 'FAIL: default lexer output changed' >&2; exit 1
fi
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
echo 'cli_test: morphology, source annotation, invalid UTF-8, size bound PASS'
