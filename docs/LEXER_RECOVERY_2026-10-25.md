# Lexer recovery: reproducible local evidence, 2026-10-25

Baseline: Grar00t/niyah-lang main b3b2b4260ab4499830d5f2aae604d8967fcd6440
Environment: Kali WSL on DESKTOP-2P1JAIV.

Before modification, tests/lexer_recovery_test.c produced 8 failures
across 12 complete-token-stream fixtures. Those failing scenarios covered
malformed UTF-8 in strings, block comments and line comments, comment error
location, decimal/hex overflow continuation, and CR-only line endings.

After modification: 13 full-stream fixtures, plus a fixed-seed 5,000-case
arbitrary-byte lexer progression/span/EOF check; these are run by make test
and by make verify-local through both GCC sanitizers and ordinary Clang.
The morphology suite independently checks 5,000 byte-corpus cases.

Accepted repairs:
- Strings: consume to unescaped close quote or next line boundary on error.
- Comments: consume to comment boundary, report a single error at the opener.
- Numbers: consume the entire overflowing run before returning E_OVF.
- Locations: count CR/LF/CRLF each as a single logical newline.
- Raw syscall: declare GCC cc clobber.
- Annotation tests: recognize metadata only by anchored record prefixes,
  preserving legitimate identifiers named IDENT_MORPH.

Reviewed without unnecessary changes:
- A 4-byte ocp branch is not reachable from current Arabic BMP-only roots.
- Keeping '-' inside raw-morph words preserves the documented Al- convention.
- EBNF already documents hexadecimal literals; clarified error behavior.
- No normalization or external Arabic lexicon imports were attempted.

Local test invocation (no cloud billing):
    make -B verify-local

GitHub Actions remains manual-only (workflow_dispatch). No hosted
workflow was invoked as part of this change. External human benefit and
Arabic neural-tokenizer improvements are not measured.
