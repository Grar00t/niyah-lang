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
