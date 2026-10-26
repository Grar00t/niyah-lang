# Milestones

## M0 Lexer

**Implemented and locally verified (2026-10-25).** Exact golden lexer output,
malformed UTF-8 inside strings/comments (full-span non-leaking recovery),
numeric overflow maximal-munch, CR/CRLF line accounting, and keyword boundaries,
5,000 fixed-seed morphology byte cases, 4 MiB input limit, and an opt-in
morphological annotation mode are covered by make test / make verify-local.
No trained NLP tokenizer or dictionary disambiguator is implied.
M1 status is tracked separately below. CI is manual-only to avoid paid runs.
- Input: `examples/مرحبا.نيّة`.
- Output: one line per token `line:col KIND text [=value]`; exit 0. Exit 2 if any T_ERROR.
- Done: `make test` prints the expected token stream; `nm niyahlex | grep ' U '` empty; `ldd niyahlex` reports not dynamic; golden file in `tests/lex/` byte-equal.
- Failures: code-point vs byte column drift; shadda (U+0651) splitting an identifier; Arabic-Indic digits not accepted; keyword prefix match (`إذان` must be IDENT); unterminated comment/string swallowing EOF; BOM.

## M1 Parser + AST dump

M1 syntax parser implemented; acceptance expanded (2026-10-27).
It recognizes declarations, functions, structs, types, blocks, if/else-if,
while, returns, assignments, calls, indexing and operator precedence.

`niyahparse --ast` is a human-readable indented dump. Separately, the C
APIs `ast_serialize` and `ast_deserialize` support an internal canonical
shape/payload format with 8 round-trip tests; they do not retain original
source locations. AST node indices differ legitimately across builds.
Limited statement-level multi-error recovery passes 2 explicit fixtures.

Verified locally: 30 inline valid / 30 inline invalid syntax cases, 3,000
arbitrary parser-byte cases, 2,000 malformed serialized-AST byte cases,
8 AST round-trips, structural equality, numeric values and bounded depth,
GCC ASan/UBSan and ordinary Clang checks, static ELF64, old goldens.
Not yet achieved: 30/30 separate golden fixture files, comprehensive error
recovery, source-level AST dump/reparse, real compiler and M2 typing.


## M2 Type checker
- Input: AST.
- Output: typed dump (type per expr, struct layouts, frame sizes); diagnostics.
- Done: every implicit conversion in `tests/type_neg/` rejected (i64↔u8, int↔bool, int↔ptr); layout of structs matches hand-computed offsets; undefined/duplicate/const-assign/missing-return all diagnosed.
- Failures: recursive struct by value; forward references to functions/structs; shadowing in nested blocks; array decay accepted by accident.

## M3 IR
- Input: typed AST.
- Output: textual IR dump (`--dump-ir`): blocks, 3AC, vregs.
- Done: IR verifier (def-before-use per path, terminator per block, type agreement) passes on all tests; hand-written IR for 10 programs equals dump.
- Failures: unreachable-block leftovers; critical `إذا`/`طالما` join with uninitialised vreg; address-taken locals kept in vregs.

## M4 Codegen + ELF (exit-code programs)
- Input: `دالة رئيسية() -> صحيح { أرجع 42; }`.
- Output: ELF64 executable; `./a.out; echo $?` prints 42; `readelf -h` valid; `strace` shows only `exit_group`.
- Done: 50 arithmetic/control-flow programs return expected codes; `objdump -d` decodes without `(bad)`.
- Failures: REX.W missing; rel32 sign; stack misaligned at call; `idiv` without `cqo`; ELF segment alignment (p_offset ≡ p_vaddr mod 0x1000); shifts needing `cl`.

## M5 Syscalls, strings, structs
- Input: hello-world using `نداء_نظام(1,1,msg,len,0,0,0)`.
- Output: prints UTF-8 Arabic text; `strace` shows exactly `write`, `exit_group`.
- Done: struct field access, pointer arithmetic, byte arrays, globals in `.data/.bss`, 6-arg calls and >6-arg spill all tested.
- Failures: string literal addresses (RIP-relative fixup); .bss not zeroed (segment memsz>filesz wrong); callee-saved regs clobbered by the generated prologue.

## M6 Compile a Niyah lexer with stage0
- Input: `compiler/lexer.نيّة` (port of `src/lexer.c`).
- Output: `niyah0`-built executable whose token dump on the test corpus is byte-identical to `niyahlex`.
- Done: `cmp` over all of `tests/lex/*.out` passes.
- Failures: missing language features discovered here (switch-like chains, byte loads, global tables); register pressure spills in `lex_next`.

## M7 Full compiler in Niyah (stage1)
- Input: `compiler/*.نيّة`.
- Output: `niyah1`; passes the entire `tests/` suite.
- Done: `niyah1` output for every test equals `niyah0` output byte-for-byte.
- Failures: stage0 miscompiles a rare construct; arena sizes; recursion depth/stack (no guard page handling).

## M8 Fixed point
- Input: `niyah1`, compiler source.
- Output: `niyah2`, `niyah3`.
- Done: `cmp niyah2 niyah3` == 0; `make bootstrap` reproduces from a clean checkout with only `cc`.
- Failures: nondeterminism (uninitialised padding written to ELF, pointer-order iteration, timestamps); stage1≠stage2 due to a latent stage0 bug (bisect by diffing `--dump-ir`).

# Historical two-week planning sketch (not an execution schedule)

The following table is the original proposed sequencing, not a verified
14-day delivery estimate or completion claim. The only verified deliverables
are the M0 toolchain and the first M1 parser/AST slice. M2 through M8 are
still unimplemented and no delivery dates are committed.

| Day | Artifacts |
|---|---|
| 1 | `src/lexer.c/.h`, `Makefile`, `tests/lex/*.nya` + golden `.out` (M0 done), docs, repo |
| 2 | M1 slice delivered: parser.h/parser.c/parser_cli.c, arena AST and precedence climbing |
| 3 | Statements, declarations, types and --ast delivered; 30 positive goldens remain open |
| 4 | Fail-fast parser errors and bounded input delivered; multi-error recovery and 30 negative goldens remain open |
| 5 | `src/types.c`: type table, struct layout, scopes, predeclared roots |
| 6 | `src/check.c`: expressions, no-coercion rules, calls/conversions; `tests/type_neg/` (M2 done) |
| 7 | `src/ir.h`, `src/irgen.c` for expressions and locals; `--dump-ir` |
| 8 | `irgen.c` control flow, calls, address-taken vars; IR verifier (M3 done) |
| 9 | `src/x86enc.c`: encoder for ~25 forms; self-test comparing bytes to a table of known encodings |
| 10 | `src/regalloc.c` (linear scan), `src/codegen.c`; `_start` stub |
| 11 | `src/elf.c` linker pass, fixups; M4: exit-code programs pass |
| 12 | Syscall intrinsic, strings, globals, structs, >6 args; M5 hello-world |
| 13 | Port lexer to `compiler/lexer.نيّة`; fix stage0 gaps it exposes |
| 14 | M6 `cmp` pass; start `compiler/ast`/`parser` port (stretch toward M7) |
