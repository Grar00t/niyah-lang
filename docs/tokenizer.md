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
