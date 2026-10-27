# Lexer and optional morphology

`niyahlex` reads UTF-8 source and prints language tokens:

```sh
./niyahlex program.niy
./niyahlex --annotate program.niy
./niyahlex --morph words.txt
```

`--annotate` adds morphology candidates for identifiers. `--morph` reads raw
words instead of programming-language syntax. Both use the optional
heuristic analyzer in `src/morph.c`; neither changes identifier bytes,
keyword recognition or compiler behavior. This analyzer has no trained
vocabulary or language-model tokenization benchmark.

## Source text

- UTF-8 must be valid, including inside comments and strings.
- Identifiers compare byte-for-byte without Unicode normalization.
- ASCII, Arabic-Indic and Eastern Arabic-Indic decimal digits are accepted.
- Arabic and ASCII comma/semicolon spellings are interchangeable.
- CR, LF and CRLF each advance one line. Columns count Unicode scalars,
  so they need not match displayed cursor positions for combining marks.
- The standalone lexer accepts at most 4 MiB and reports read or output errors.

The lexical rules and operator precedence are in [grammar.ebnf](grammar.ebnf).

## Morphology output

The original word is always the first candidate. Further candidates suggest
clitic boundaries, roots or patterns without choosing a grammatical meaning.
Every candidate refers to the original word's byte ranges.

The analyzer accepts at most 96 base characters and emits at most 64 analyses
with eight parts each. Oversized words retain an intact raw fallback and an
explicit truncation status. In `--annotate`, that status does not invalidate
the language identifier; in raw `--morph` mode it causes a nonzero exit.
Malformed UTF-8 is rejected.

Raw word segmentation accepts Unicode whitespace in addition to its ASCII
punctuation and directional-mark separators. Joiners, zero-width spaces,
interior BOM and U+180E are not whitespace separators. Harakat and tatweel
remain in the word. Hyphens remain available for the supported `al-`
transliteration prefix.

## C API

`lex_init` and `lex_next` borrow the source buffer; returned tokens contain
byte slices, line/column coordinates and integer values. Keep the source
alive while using those tokens or a parser AST derived from them.

`morph_analyze` uses caller-owned storage and performs no allocation.
Its schema and limits are declared in `src/morph.h`.
