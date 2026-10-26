#!/bin/sh
set -eu
export LC_ALL=C
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# Compare the entire CLI output with analyses of each word in isolation.
# Only the source coordinates differ; raw UTF-8 bytes and candidates must match.
printf 'والكتاب' > "$tmp/first.txt"
printf 'سيكتب' > "$tmp/second.txt"
./niyahlex --morph "$tmp/first.txt" > "$tmp/first.out"
./niyahlex --morph "$tmp/second.txt" > "$tmp/second.out"
grep -Fq 'CONJ_W(و) + DET_AL(ال) + STEM(كتاب)' "$tmp/first.out"
grep -Fq 'FUTURE_S(س) + IMPF_Y(ي) + STEM(كتب)' "$tmp/second.out"

cases=0
check_pair() {
    label=$1
    printf '%bوالكتاب%bسيكتب%b' "$2" "$3" "$4" > "$tmp/input"
    sed "1s/^1:1/$5/" "$tmp/first.out" > "$tmp/expected"
    sed "1s/^1:1/$6/" "$tmp/second.out" >> "$tmp/expected"
    ./niyahlex --morph "$tmp/input" > "$tmp/actual"
    if ! cmp "$tmp/expected" "$tmp/actual"; then
        echo "FAIL: independent word analyses for $label" >&2
        exit 1
    fi
    cases=$((cases + 1))
}

# Unicode 17.0 White_Space, with explicit UTF-8 octets for POSIX printf.
# https://www.unicode.org/Public/17.0.0/ucd/PropList.txt
# Preserve the existing CR/LF-only line numbering; other scalars advance col.
while read -r label bytes second_position; do
    check_pair "$label" '' "$bytes" '' 1:1 "$second_position"
done <<'SPACES'
U+00A0 \302\240 1:9
U+0009 \011 1:9
U+000A \012 2:1
U+000B \013 1:9
U+000C \014 1:9
U+000D \015 2:1
U+0020 \040 1:9
U+0085 \302\205 1:9
U+1680 \341\232\200 1:9
U+2000 \342\200\200 1:9
U+2001 \342\200\201 1:9
U+2002 \342\200\202 1:9
U+2003 \342\200\203 1:9
U+2004 \342\200\204 1:9
U+2005 \342\200\205 1:9
U+2006 \342\200\206 1:9
U+2007 \342\200\207 1:9
U+2008 \342\200\210 1:9
U+2009 \342\200\211 1:9
U+200A \342\200\212 1:9
U+2028 \342\200\250 1:9
U+2029 \342\200\251 1:9
U+202F \342\200\257 1:9
U+205F \342\201\237 1:9
U+3000 \343\200\200 1:9
SPACES

# Existing Arabic punctuation and directional separators still split words.
while read -r label bytes; do
    check_pair "$label" '' "$bytes" '' 1:1 1:9
done <<'ARABIC'
Arabic-comma \330\214
Arabic-semicolon \330\233
Arabic-question-mark \330\237
ALM \330\234
LRM \342\200\216
RLM \342\200\217
ARABIC
check_pair CRLF '' '\r\n' '' 1:1 2:1
check_pair mixed '\302\240\t' '\342\200\211\302\240\342\200\257' '\343\200\200\n' 1:3 1:13
check_pair BOM '\357\273\277' '\302\240' '' 1:1 1:9

# Empty words must not be emitted for empty or separator-only inputs.
for bytes in '' '\302\240\342\200\257\t\r\n\343\200\200'; do
    printf '%b' "$bytes" > "$tmp/input"
    ./niyahlex --morph "$tmp/input" > "$tmp/actual"
    test ! -s "$tmp/actual"
    cases=$((cases + 1))
done

# Do not turn adjacent format controls, harakat or tatweel into word breaks.
# U+200B/C/D, U+2060, interior U+FEFF and U+180E are not White_Space.
while read -r label bytes; do
    printf 'و%bكتاب' "$bytes" > "$tmp/input"
    ./niyahlex --morph "$tmp/input" > "$tmp/actual"
    awk -F '\t' '$2 == "WORD" { print $3 }' "$tmp/actual" > "$tmp/words"
    cat "$tmp/input" > "$tmp/expected"
    printf '\n' >> "$tmp/expected"
    if ! cmp "$tmp/expected" "$tmp/words"; then
        echo "FAIL: raw bytes or word boundaries changed for $label" >&2
        exit 1
    fi
    cases=$((cases + 1))
done <<'NONSPACES'
ZWSP \342\200\213
ZWNJ \342\200\214
ZWJ \342\200\215
WORD-JOINER \342\201\240
interior-BOM \357\273\277
MVS \341\240\216
fatha-shadda \331\216\331\221
tatweel \331\200
NONSPACES

# Malformed/truncated UTF-8 must still fail beside a valid separator.
for bytes in '\302' '\342\200' '\240' '\355\240\200'; do
    printf 'والكتاب\302\240%b' "$bytes" > "$tmp/input"
    rc=0
    ./niyahlex --morph "$tmp/input" > "$tmp/actual" || rc=$?
    test "$rc" -eq 2
    grep -aFq 'status=1' "$tmp/actual"
    cases=$((cases + 1))
done

# Raw-text splitting must not expand the programming lexer's whitespace rules.
printf 'والكتاب\302\240سيكتب' > "$tmp/input"
printf '1:1\tIDENT\tوالكتاب\n1:8\tERROR\t\302\240\tinvalid UTF-8 or unexpected character\n1:9\tIDENT\tسيكتب\n1:14\tEOF\n' > "$tmp/expected"
rc=0
./niyahlex "$tmp/input" > "$tmp/actual" || rc=$?
test "$rc" -eq 2
cmp "$tmp/expected" "$tmp/actual"
cases=$((cases + 1))

echo "morph_whitespace_test: $cases Unicode/Arabic boundary, byte-preservation and compatibility cases PASS"
