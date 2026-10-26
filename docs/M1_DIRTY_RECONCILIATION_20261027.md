# M1 dirty-checkout reconciliation — 2026-10-27

## Scope and observed evidence

The old, dirty checkout `/home/a/niyah-inspect-20261026-215744/repo`
had an uncommitted patch (242 insertions and one deletion) in
`src/parser.c`, `src/parser.h`, and `tests/parser_test.c`.
It adds `ast_serialize`, `ast_deserialize`, and seven round-trip cases,
but its `trees_match` compares arena node IDs and so reports false
mismatches for syntactically equivalent trees when allocation order differs.

These capabilities are **already on main**, via `907af0c`:
- Round-trip traverses AST shapes rather than assuming identical arena IDs.
- Eight fixtures include escaped strings, not just the older seven.
- AST deserialization validates arguments, payload token classes,
  syntax, operator kinds, and source token reconstruction.
- Serialization bounds allocation, traversal depth, and output buffers.
- 2,000 serialized-byte corpus cases and malformed-AST rejection tests
  supplement the original 3,000 parser-byte and syntax cases.

**Decision:** do not copy the older 242-line patch over the newer implementation.
No uncommitted source code was silently discarded or reset.

## Local inventory and backup

A read-only inventory of 12 niyah-lang-related local checkouts was saved to:
`D:\NiyahLangReview-20261025\WORKTREE_AUDIT_20261027.json`.
It records exact paths, HEAD, branch, dirty/untracked counts and classifications.
Some older clones do not hold the newest Git objects, so the absence of an
ancestor comparison **is not proof that they have no unique commits**.

One dirty checkout has three modified tracked files and 17 untracked files,
including backup files and logs. Its full directory was archived at:
`D:\NiyahLangReview-20261025\niyah-inspect-dirty-20261027.tar.gz`.
The tar archive was listed successfully after creation.
SHA-256:
`ee63318540fc08f458249cef2efdd8923e10041a3b4e86562293a4e775c1602b`.

This backup is **local**, not committed to GitHub: keep unreviewed logs,
personal paths and untracked artifacts off the public repository.

## Verification

`make -B -C /home/a/niyah-lang-canonical-20261027 verify-local`
finished with exit code 0:
`LOCAL_VERIFY_PASS`, `GCC_ASAN_UBSAN_PASS`,
`CLANG_COMPILE_AND_TEST_PASS`, `ELF64_STATIC_CHECK_PASS`.
The parser suite includes 30 valid, 30 invalid, two multi-error,
eight round-trip fixtures, AST hardening and random input coverage.
The raw morphology regression includes 49 Unicode whitespace cases.
No hosted GitHub Actions were launched.

## Cleanup gate

Zero worktree directories were deleted. Do not auto-delete a clean
checkout merely because it has no uncommitted files: branch-specific
commits, .git metadata, references and local-only receipts may still matter.
Compare every clone to the published main **after fetching**, preserve
unique commits and make a restore test before any future removal.

Source-of-truth checkout: `/home/a/niyah-lang-canonical-20261027`.
Published source of truth: `Grar00t/niyah-lang` branch `main`.
The M2 scalar checker remains partial; no compiler completion claim.
