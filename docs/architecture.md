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
| `src/codegen.[ch]`, `src/compile_cli.c` | Direct ELF generation and freestanding production CLI |
| `src/tensor_core.[ch]` | Intrinsic AST annotation, typed lowering, allocation and tensor ELF emission |
| `src/compiler_start.s`, `src/compiler_native.c` | Entry point, raw syscall ABI and bounded runtime support |

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

For a binary operator with an integer literal as its right operand, the
emitter evaluates the left operand first and loads the literal directly into
`rcx`, preserving the left value in `rax`. It uses a sign-extended immediate
only within signed 32-bit range and a full 64-bit immediate otherwise.
Other right operands keep the stack-based path; unary expressions are not
folded. Both paths use the same operator checks. The register contract is
documented next to the emitter's internal types in `src/codegen.c`.

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

`make test` exercises language, CLI and execution behavior. Native cases
check the public exit status modulo 256 and independently compare the full
signed 64-bit result against the interpreter. A test-only helper uses Linux
`ptrace` on its own child, observes RDI at entry to the exit syscall after a
successful exec, and confirms normal termination. It does not modify the
generated ELF; unavailable tracing fails the test.

The full-value check also enforces an internal invariant: the entry bridge
preserves the entire result of `رئيسية()` in the exit-syscall argument.
A temporary compiler mutation that adds 256 to that result is rejected,
although it preserves the public 8-bit exit contract. Fixtures cover high
bits in arithmetic, comparisons, branches, shifts and function calls.

Parser nesting, semantic expression depth and native statement layout have
separate bounded traversals. Frame layout visits declarations and statement
bodies, without charging expression subtrees to the surrounding block's
depth. Boundary tests cover accepted and rejected nesting and expressions.

`make verify-local` additionally builds and tests the host tools with GCC
ASan/UBSan and ordinary Clang. Sanitizers instrument the host C tools, not
the emitted machine code. Tests and diagnostic code are kept in `tests/`.

## Tensor integration

Tensor intrinsics are core AST nodes. The production compiler lowers and
allocates them in memory, then emits guarded native code without launching
helpers. The scalar interpreter remains hosted. The diagram above describes
the scalar path; [TENSOR_AST.md](TENSOR_AST.md) defines the tensor path,
its limits, backend dispatch and byte-parity requirements.
