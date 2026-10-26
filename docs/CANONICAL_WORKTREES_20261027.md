# Canonical checkout and local worktree policy (2026-10-27)

The GitHub `main` branch is the sole publication source for `niyah-lang`.
The preferred, locally tested development checkout is:

    /home/a/niyah-lang-canonical-20261027

This checkout was created from `main` at `f202b1f`; the Unicode whitespace
fix from PR #3 was cherry-picked without importing its old base. To verify:

    cd /home/a/niyah-lang-canonical-20261027
    make -B verify-local
    git status --short --branch

No hosted GitHub Actions were requested. The workflow is manual-only.
The original historical pre-run billing failure is recorded in
`docs/MORPH_WHITESPACE_2026-10-26.md`; do not mistake it for a compiler test
failure, or claim it proves today's billing/account status.

## Preserve and review before cleaning

These directories are work and evidence, not disposable cache:

- `/home/a/niyah-inspect-20261026-215744/repo`: a dirty older checkout with
  unstaged parser changes, `.bak` files, and logs. **Do not delete or reset**.
  It contains source and evidence requiring separate comparison.
- `/home/a/niyah-lang-review-20261025` and associated worktrees for M0,
  lexical recovery and initial M1: historical implementation checkpoints.
- `/home/a/niyah-lang-m1-roundtrip-20261027` and its release checkout:
  M1 serializer/round-trip development and tested receipt.
- `/home/a/niyah-lang-m2-20261027`: the working M2 scalar checker checkout.
- `/home/a/niyah-lang-release-*`: historical detached release checkouts;
  verify clean status and link/ownership before considering removal.

`git worktree list` must be run separately for each repository family;
some directories are Git worktrees of different local clones. Never use
`git clean -fd`, `git reset --hard`, broad recursive deletes, or force
pushes to tidy this workspace. Cleanup should follow a saved inventory and
user review of untracked data and unique commits. The canonical checkout is
a new location, **not a deletion or merge of dirty historical state**.

## GitHub pull request reconciliation

- PR #2 is already merged and represented in main; no duplicate merge.
- PR #3 contains the Unicode whitespace fix. Its single commit was replayed
  on the current main and tested locally, including the 49 regressions.
  Never merge its old branch blindly: it is based on an earlier commit.
- PR #1 is an old lexer fix plus automatic CI triggers. Its base predates
  subsequent lexer fixes and its workflow triggers conflict with the present
  no-hosted-Actions rule. Keep it for review; do not merge unchanged.
- Do not delete remote branches without proof that unique work is preserved.

## Verified scope

`make -B verify-local` on this integration passed: original token/AST
fixtures, 49 Unicode whitespace cases, M0 lexer/morph random-byte suites,
M1 30 positive/30 negative and round-trip suites, M2 7 positive/13 negative,
GCC ASan/UBSan, ordinary Clang, and ELF64 static checks.

The M2 checker remains a partial C API and does not constitute native
compilation or full type checking. No measured user benefit is claimed.
