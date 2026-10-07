# niyah-lang

Self-hosting compiler for نيّة (Niyah), a statically typed language with Arabic keywords. C + x86-64 asm. Zero dependencies.

## Project statement

- Target: Linux x86-64. Output: static ELF64, raw syscalls, no runtime, no libc.
- Backend: direct x86-64 code generation. No VM, no LLVM, no external assembler or linker.
- Types: static, strict, no implicit coercion.
- Self-hosting: the final compiler is produced by compiling its own Niyah source. Chain: `cc` -> `niyah0` (C) -> `niyah1` -> `niyah2` -> `niyah3`; success is `cmp niyah2 niyah3`. See `docs/architecture.md`.
- Keywords: `إذا` if, `وإلا` else, `طالما` while, `أرجع` return, `متغير` var, `ثابت` const, `دالة` function, `هيكل` struct (extension required for self-hosting).

## Status

M0 (lexer) implemented. Everything else: see `docs/milestones.md`.

## Build

Requires only a C11 compiler (stage0) and `make`.

```
make            # builds ./niyahlex (freestanding, static, no libc)
make test       # dumps tokens for examples/مرحبا.نيّة
make test | diff - tests/lex/مرحبا.expected   # note: strip the make echo lines first
./niyahlex FILE # tokens of FILE; exit 0 ok, 1 I/O error, 2 lex error
```

## Layout

```
docs/architecture.md   pipeline, IR decision, bootstrap chain
docs/grammar.ebnf      lexical + syntactic grammar
docs/milestones.md     M0..M8 and the two-week plan
src/lexer.[ch]         stage0 lexer
examples/              sample programs
tests/lex/             golden token streams
```

## License

MIT.
