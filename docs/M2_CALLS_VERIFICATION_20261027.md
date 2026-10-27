# Scalar call checking: local verification, 2026-10-27

Base commit: `15b4ce82eb0a78643e33a4c098c7204204aa0cb6`.

## Behavior delivered

`niyahparse --check` checks named function calls using predeclared scalar
signatures. It accepts forward declarations, direct and mutual recursion,
exact scalar parameters/results, and standalone void calls. It rejects
wrong arity or argument types, non-function targets, indirect/chained calls,
and void results in value contexts. Function and global names still share
one namespace; local scalar shadowing makes the name non-callable.

This is type checking. The hosted constant-expression runner does not
execute calls, and native code generation/self-hosting are still absent.

## Executed evidence

- `make -B verify-local`: exit 0, `LOCAL_VERIFY_PASS`.
- Checker C fixtures: 24 positive and 48 negative, plus metadata reset.
- Semantic CLI fixtures: 6 valid and 15 rejected, plus M1 compatibility,
  usage and stdout-failure checks.
- Expanded C tests against the original base source: exit 1 with 39 failed
  expectations (including the updated bare-function diagnostic).
- Existing lexer, parser, morphology, recovery, AST round-trip, fuzz and
  constant-runner regressions passed.
- GCC 16.2.0 ASan/UBSan gate passed, including the expanded checker tests.
- Ordinary Clang gate passed. Clang sanitizers are not claimed.
- Lexer and parser passed static ELF64 checks.
- `git diff --check` passed.
- GitHub Actions was not launched; its workflow remains manual-only.

Verification log SHA-256: `466ebd9f6eb40fdebd06f3a9d23244c82133947e61bc9de31d4f8c227f6f2ea1`.

The recorded log is local evidence, not a hosted CI result. These checks do
not establish full M2 coverage, native Niyah execution or portability to
every platform.
