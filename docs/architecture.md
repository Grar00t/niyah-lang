# Implementation

The project targets Linux x86-64 and uses C11. Each invocation reads one
source file.

```text
UTF-8 source → lexer → parser → AST → scalar checker
                                      ├─ interpreter → integer result
                                      └─ x86-64 emitter → ELF executable
```

| Files | Responsibility |
| --- | --- |
| `src/lexer.[ch]` | UTF-8 validation and language tokens |
| `src/morph.[ch]` | Optional morphology candidates; outside compiler semantics |
| `src/parser.[ch]` | Recursive-descent parser, precedence and arena AST |
| `src/check.[ch]` | Scalar types, bindings, function signatures and return paths |
| `src/eval.[ch]`, `src/run_cli.c` | Scalar interpreter and hosted CLI |
| `src/codegen.[ch]`, `src/compile_cli.c` | Machine-code generation, ELF output and hosted CLI |

## Front end

Tokens borrow slices of the input buffer. AST nodes use one-based arena
indices and ordered child/sibling links; zero denotes an absent node.
Source bytes and the arena must remain alive while any consumer uses them.

The parser recognizes more syntax than the scalar checker accepts. The
checker rejects unsupported forms, compares identifier bytes without
normalization, predeclares function signatures and maintains lexical
scopes. It checks exact argument counts/types and conservatively requires
all paths in a value-returning function to return.

The parser's C serialization API preserves ordered AST structure and token
payloads, but not source positions or original formatting. The CLI's
`--ast` output is a separate human-readable format.

## Execution

The interpreter evaluates the checked AST. The native compiler emits
x86-64 bytes and an ELF64 executable directly, with no external assembler,
linker or LLVM backend. Compiler tools are built using the host C compiler;
generated programs use Linux syscalls and do not link libc.

Both execution paths support scalar locals/globals, constants, branches,
loops and named calls. Initializers run in source order. An uninitialized
local cannot be read; a global without an initializer becomes zero at its
declaration. Reading a global before it has been initialized or assigned fails,
including through a called function. Assigning a later global initializes
it, but its declaration still runs when reached and replaces that value.

Integer arithmetic checks signed overflow. Division by zero and shift
counts outside 0–63 fail. Left shifts use checked signed arithmetic; right
shifts are arithmetic. `&&` and `||` short-circuit. Execution has bounded
statement fuel and call depth. Details and error codes are in the
[language reference](language.md).

`niyahc` publishes a new output file and refuses to replace an existing
path. Its generated entry point calls `رئيسية()` and exits with the result
modulo 256. Runtime errors print a diagnostic and use a dedicated nonzero
exit status.

## Verification

`make test` exercises language, CLI and execution behavior. `make
verify-local` adds local sanitizer and Clang checks. Tests and diagnostic
code are kept in `tests/`.
