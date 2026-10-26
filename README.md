# niyah-lang

Target: self-hosting compiler for نيّة (Niyah), a statically typed language with Arabic keywords. Current implementation: C11 M0 lexer and M1 parser. No generated native program yet.

## Project statement

- Target: Linux x86-64. Output: static ELF64, raw syscalls, no runtime, no libc.
- Backend: direct x86-64 code generation. No VM, no LLVM, no external assembler or linker.
- Types: static, strict, no implicit coercion.
- Self-hosting: the final compiler is produced by compiling its own Niyah source. Chain: `cc` -> `niyah0` (C) -> `niyah1` -> `niyah2` -> `niyah3`; success is `cmp niyah2 niyah3`. See `docs/architecture.md`.
- Keywords: `إذا` if, `وإلا` else, `طالما` while, `أرجع` return, `متغير` var, `ثابت` const, `دالة` function, `هيكل` struct (extension required for self-hosting).

## Status

M0 is implemented: Unicode-aware C11 lexer and optional Arabic morphology
candidate analysis. M1 has a working syntax parser and arena-backed AST.
This is NOT yet a native Niyah compiler: M2 type checking, IR, code
generation, executable emission and self-hosting remain unimplemented.
M2 has a first, independently tested **scalar type and scope checker C API**:
`checker_init` / `checker_run` in `src/check.[ch]`. It recognizes
`صحيح`, `بايت`, `منطقي`, block scopes, locals, parameters, const
assignment and strict scalar expressions. Full M2 is **NOT complete**:
function calls, structs, pointers, arrays, flow-completeness, type layouts,
and integration into the static `niyahparse` CLI remain open.
Morphology never changes programming-language identifier identity.

M1 now has 30 positive / 30 negative syntax fixtures, two multi-error
recovery tests, and eight canonical AST serialize/deserialize round-trips.
The internal C API retains AST structure and integer values but not original
source positions. This is separate from the human-readable CLI `--ast`
output; neither format can recreate the original source file.

## Build

Requires a C11 compiler, GNU Make and Linux x86-64:

    make                  # builds both freestanding, static ELF tools
    make test             # lexer, morphology, parser, CLI and golden AST
    make verify-local     # tests + GCC ASan/UBSan + ordinary Clang tests

    ./niyahlex FILE             # M0 language token dump
    ./niyahlex --morph FILE     # optional raw-text morphology candidates
    ./niyahlex --annotate FILE  # optional identifier annotations
    ./niyahparse FILE           # syntax validation: OK or diagnostic
    ./niyahparse --ast FILE     # deterministic indented AST

Exit codes for niyahparse: 0 valid; 1 usage/file; 2 syntax/lexical error;
3 failed output write. This parses source but does NOT generate executables.
For limitations see docs/M1_PARSER_2026-10-25.md.
GitHub Actions remains manual-only to avoid billing.


## Layout

```
docs/architecture.md   pipeline, IR decision, bootstrap chain
docs/grammar.ebnf      lexical + syntactic grammar
docs/milestones.md     M0..M8 and the two-week plan
src/lexer.[ch]         M0 compiler lexer
src/morph.[ch]         opt-in morphology candidate layer
src/parser.[ch]        M1 recursive descent and arena AST
src/parser_cli.c       freestanding syntax checker / AST dumper
examples/              sample source programs
tests/lex/             M0 golden lexer output
tests/parse/           M1 golden AST output
tests/parser_test.c    parser correctness, AST and bounds tests
```

## License

MIT.
