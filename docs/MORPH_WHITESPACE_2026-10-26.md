# Morph Unicode whitespace verification — 2026-10-26

Base: `654983aa4533b91df1da96615204500ad5b6176c` (`main`).
Review finding: [PR #2, P2](https://github.com/Grar00t/niyah-lang/pull/2#discussion_r4370359569).
Execution environment: Ubuntu 24.04.3 LTS container, Linux x86_64, GCC 13.3.0.
This receipt does not assert execution on the user's Windows or WSL machine.

## Change and regression proof

Before the fix, `والكتاب<U+00A0>سيكتب` produced one WORD record and only a
raw STEM candidate, with CLI exit 0. The new regression fails against the
unchanged source (exit 1). After the fix, the same bytes yield two WORD
records, preserving both words and their independent clitic candidates.

`morph_separator` now covers Unicode 17.0 White_Space while preserving
existing C0, punctuation and bidi separators. It remains scoped to `--morph`:
no source normalization, changes to `lex_next`, `skip_trivia`, UTF-8 decoding,
line numbering, morphology candidate generation or public headers.

The 49 CLI regression cases cover all 25 White_Space scalars, Arabic
punctuation and bidi markers, CRLF, mixed/leading/trailing separators, BOM,
empty input, non-whitespace format controls, harakat, tatweel, malformed and
truncated UTF-8, and the unchanged programming-lexer NBSP error stream.
Pair cases compare complete output against each word analyzed separately,
including candidates, raw bytes, positions and record counts.
The suite is part of `make test`, so local and manually triggered CI run it.

## Executed commands

| Revision / command | Exit | Result |
| --- | ---: | --- |
| Baseline `make test` | 0 | Existing unit, CLI and golden lexer/AST tests passed |
| New `sh tests/morph_whitespace_test.sh` before source fix | 1 | Fails on U+00A0 independent-word output |
| Patched `make -B verify-local` | 2 | All ordinary tests passed; LeakSanitizer then failed to inspect `/proc` |
| Patched `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 make verify-local` | 0 | Ordinary tests, GCC ASan/UBSan unit suites and static ELF64 checks passed |

Actual stdout/stderr and exit codes are in
[the execution log](verification/morph-unicode-whitespace-2026-10-26.log).
The ordinary suites include 5,000 lexer byte-corpus cases, 5,000 morphology
byte-corpus cases and 3,000 parser byte-corpus cases, plus their fixtures.
The new raw CLI regressions run against the freestanding ELF, not an
ASan-instrumented standalone CLI. ASan/UBSan instrument the existing C unit
suites. Leak checking is **not verified**: the initial runtime failed with
`Can't open /proc/.../task for reading` and `LeakSanitizer has encountered a
fatal error`. Only the retry's process environment disabled leak detection;
no acceptance scripts or CI sanitizer settings were weakened.
Clang was not installed, so its optional checks did not run.

## GitHub Actions: infrastructure failure, not a test failure

[Run 40181315086](https://github.com/Grar00t/niyah-lang/actions/runs/40181315086)
ran for merge commit `eec4277ffaa8541f8087aab675092c3e291a48ce`.
Its only job, [lexer / 121132796958](https://github.com/Grar00t/niyah-lang/actions/runs/40181315086/job/121132796958),
has `steps: []`, `runner_id: 0` and no runner name. The check-run
[annotation endpoint](https://api.github.com/repos/Grar00t/niyah-lang/check-runs/121132796958/annotations)
returned this explicit message:

> The job was not started because your account is locked due to a billing issue.

Job-log retrieval returned HTTP 404 / BlobNotFound. There is no evidence
that this job executed checkout, compilation or tests. The billing lock is
the recorded reason for this historical run; no claim is made about the
account's present billing status. The current workflow intentionally uses
only `workflow_dispatch` (changed after that run). It is preserved; no billed
workflow rerun or account change was attempted. Hosted tests remain unverified.

## SHA-256 of tested files

The executable hashes identify the local GCC-built artifacts; binaries are
not committed. The source and regression hashes identify the tested change.

```text
9280f638dda836b4c02d7caf7d2ba2b1bd36ef4f48e8c5d965a4011b794ad3ac  src/lexer.c
c015d9b801ca86e7ec62b2722407d180e4e98903d469be4a147c53dcc986ee25  tests/morph_whitespace_test.sh
1c2f1545f3717ce4e0546975a22e5873aa65630470733442a9b0686b381c3178  niyahlex
39e22d0e5f28e5b4701fcc93bd434b7bad0192ec24e7be450f77d7512b63843f  niyahparse
6fb2851407c9cd735e059505bf99c622f09efde17ba3a63700ad9dcd3f3de1d2  docs/verification/morph-unicode-whitespace-2026-10-26.log
```
