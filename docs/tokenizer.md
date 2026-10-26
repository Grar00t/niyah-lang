# Arabic tokenization and morphology (M0 extension)

The programming-language lexer and Arabic morphology analyzer are separate, bounded,
deterministic functions. The lexer recognizes keywords, identifiers, punctuation,
numbers and UTF-8; the analyzer proposes candidate boundaries, roots and patterns.

## CLI

- ./niyahlex FILE: unchanged language token stream (golden output preserved).
- ./niyahlex --morph FILE: raw text words and all morphology candidates.
- ./niyahlex --annotate FILE: unchanged language tokens, plus candidate analyses
  only for identifiers (IDENT_MORPH). Keywords and source bytes are unchanged.

Build with make; verify with make test. The CLI limits input to 4 MiB and rejects
oversize input or failed reads instead of silently truncating them.

Example:

    printf 'والكتاب سيكتب مكتوب\n' > /tmp/niyah-words.txt
    ./niyahlex --morph /tmp/niyah-words.txt

Inspect CONJ_W + DET_AL + STEM, FUTURE_S + IMPF_Y + STEM and pattern=MAFOOL.
These are candidate annotations; they do not establish actual grammatical meaning.

### Raw-text word separators

`--morph` recognizes all Unicode 17.0 `White_Space` code points, including
NBSP (U+00A0), U+0085, U+1680, U+2000..U+200A, U+2028/U+2029,
narrow NBSP (U+202F), U+205F and U+3000. The existing ASCII control,
punctuation and ALM/LRM/RLM separators are retained. The property list is
defined in [Unicode PropList.txt](https://www.unicode.org/Public/17.0.0/ucd/PropList.txt).

Words separated by these characters are analyzed independently; each word's
original UTF-8 bytes remain intact. No Unicode normalization is performed.
Zero-width space/joiners, word joiner, interior BOM and U+180E are not added
as separators. Harakat and tatweel remain inside words. A leading BOM is
still skipped by the existing input initialization.

This change only affects raw-text `--morph` segmentation. Language tokenization
and `--annotate` keep their existing rules. Location tracking also keeps the
existing convention: CR, LF and CRLF advance the line; other separators
advance the Unicode scalar column. `make test` runs the complete-output
regressions in `tests/morph_whitespace_test.sh`.

## Limitations

- The raw word is always candidate zero, intact and byte-exact.
- Not BPE/SentencePiece. No trained token vocabulary, embeddings, generative model,
  or general Arabic NLP accuracy claim.
- Heuristic segmentation: 10 seed verbs and selected patterns; no robust dialect
  coverage, Unicode NFC normalization or semantic disambiguation.
- Invalid UTF-8 fails. Input >96 base code points returns a raw fallback and
  TOO_LONG. At most 64 analyses per word; truncation is signalled.
- No parser, type checker, code generator or ELF compiler implementation yet.

## Identifier and model-tokenizer distinction

The lexer makes one T_IDENT token for a valid Niyah identifier even when it
has several Arabic root/clitic candidates. Morphological output does not
change that token's byte range, spelling, keyword status or identity.
The optional --annotate display is a diagnostic adjunct, not a compiler phase.

Code-point columns in M0 are Unicode scalar offsets, not grapheme clusters:
combining shadda or harakat may share a displayed cluster with a base.
UTF-8 validity is enforced inside comments and string literals as well as
ordinary code. Format controls accepted between code tokens can still
confuse bidirectional presentation, and tatweel or decomposed spellings can
produce visually similar but byte-distinct identifiers. Do not treat raw
code-point columns as guaranteed cursor positions in every editor.

This is not BPE, SentencePiece, UnigramLM or a neural model tokenizer. Most
modern subword tokenizers preserve UTF-8 text reversibly but can split Arabic
words into many units; validating UTF-8 does not solve token efficiency or
contextual Arabic morphology. No model benchmark is claimed.

The bounded morphology is heuristic and emits alternatives, not unique
semantics. Future lexicon integration requires a versioned license-proven
dataset with recorded provenance and independently evaluated gold labels.
See docs/RESEARCH_2026-10-25.md.

The standalone CLI exits nonzero on failed stdout writes (including /dev/full),
so downstream file pipelines do not silently accept truncated token output.

An identifier longer than the morphology limit is still a valid lexer token.
In --annotate mode, TOO_LONG is shown with the intact raw fallback and
truncated=1 but does not make the lexical scan fail. In --morph raw-text mode,
TOO_LONG remains a nonzero exit so callers can reject incomplete analyses.

## Lexical recovery contract (2026-10-25)

- A string with invalid UTF-8 yields one T_ERROR spanning the invalid
  quoted region up to its actual unescaped closing quote (or the next CR/LF).
  A bad escape uses the same recovery boundary. No keywords from the invalid
  string are exposed in the subsequent token stream.
- A malformed UTF-8 line or block comment yields one T_ERROR spanning the
  entire comment, anchored at its opening delimiter. Unterminated block
  comments report E_COMMENT at the opener, not a zero-length error at EOF.
  Source outside the comment remains visible in later tokens.
- Overflowed decimal/hex literals produce one error spanning the entire digit
  run; trailing arithmetic punctuation and valid tokens remain separate.
- CR, LF and CRLF each count as one logical line ending. Columns remain
  Unicode scalar counts, not grapheme-cluster or visual cursor positions.
- The sys3 inline syscall wrapper declares the condition-code clobber.
  This is a compiler constraint, not a new syscall instruction or runtime.
- Default language tokens are unchanged for valid existing programs.
  An annotation record is identified by its full record prefix, not by
  searching for the string IDENT_MORPH inside arbitrary source text.
- Hyphen is intentionally retained within --morph raw words, including
  the explicitly supported Latin transliteration Al-/al-/AL-; splitting every
  hyphen would break the existing DET_AL candidate convention.
- Morphology root letters are restricted to Arabic BMP code points by
  root_letter()/arabic() in src/morph.c; the current ocp() three-byte UTF-8
  encoder is not a general Unicode printer. It is only used with those
  BMP-limited root candidates. An unrestricted code-point printer would need
  a four-byte UTF-8 branch and separate tests.

Run make -B verify-local inside Kali to validate the current revision.
The suite contains full-stream recovery fixtures and a fixed-seed 5,000-case
lexer byte corpus, plus the independent 5,000-case morphology byte corpus.
These checks verify the tested contracts only; they are not a language-model
token-efficiency benchmark or a full Unicode identifier conformance suite.
