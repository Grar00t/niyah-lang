# Niyah | Arabic to native code

A C11 compiler for Arabic-keyword source on Linux x86-64. The production
`niyahc` executable emits static ELF64 directly: no libc, dynamic loader,
compiler subprocess, or machine-learning framework at compile/run time.

[Language](docs/language.md) / [Architecture](docs/architecture.md) /
[Tensor AST](docs/TENSOR_AST.md) / [Verification](docs/VERIFICATION.md)

```text
UTF-8 source -> lexer -> arena AST
                          |-- scalar checks -> x86-64 emitter
                          `-- tensor intrinsics -> typed IR -> live intervals
                                                -> allocation -> ISA dispatcher
                                                               -> static ELF64
```

## Build the matched pair

The native backend is a companion source repository, not a downloaded model.
Keep both checkouts side by side. These review branches are not yet merged.

```sh
git clone --branch review/nexus-native-20261027 https://github.com/Grar00t/niyah-assembly-model.git
git clone --branch review/nexus-ast-core-20261027 https://github.com/Grar00t/niyah-lang.git
make -C niyah-lang -j2 all NIYAH_MODEL=../niyah-assembly-model
make -C niyah-lang test-ast-tensor NIYAH_MODEL=../niyah-assembly-model
```

Build tools: Linux x86-64, GCC-compatible C11 compiler, GNU Make/binutils,
and Python 3.10+ standard library. Windows users build inside WSL.
Full verification additionally uses Clang, sanitizers, `timeout`, and
parent-child `ptrace` permission. Nothing here needs MSVC, CUDA or PyTorch.
Compatible source pins are in [integration-lock.json](docs/integration-lock.json).

## Compile a tensor program

The following example is checked into `examples/tensor_intrinsics.niyah`.

```text
دالة رئيسية() -> صحيح {
    tensor_reserve(1);
    متغير والكتاب: i8x16x64 = tensor_input_a();
    متغير سيكتب: i8x64x16 = tensor_input_b();
    ثابت نتيجة: i32x16x16 = tensor_gemm(والكتاب, سيكتب);
    tensor_emit(نتيجة);
    أرجع 0;
}
```

```sh
cd niyah-lang
./niyahc -o tensor.elf examples/tensor_intrinsics.niyah
./tensor.elf features
```

`features` prints a 64-byte binary capability record; it is not text.
Normal tensor execution reads packed input matrices from stdin and writes
INT32 output to stdout. The compiler refuses to overwrite an existing output.

## Contracts

| Surface | Supported behavior |
| --- | --- |
| Scalar language | Strict integer/byte/boolean types; functions, scopes, branches, loops and checked arithmetic |
| Tensor language | One straight-line main; signed INT8 16x64 times 64x16 -> INT32 16x16 |
| Register assignment | At most 64 values, 128 operations, 8 slots; half-open live intervals; no spills |
| Arabic morphology | Original UTF-8 spans and candidate roots; never symbol aliases or trained embeddings |
| Backend order | AMX -> AVX-512 -> AVX2 -> Scalar, after CPU and OS-state checks |
| Published compiler | Static Linux ELF64; raw syscalls; no adjacent helper required |
| Interpreter/tests | `niyahrun` and explicitly hosted test drivers may use libc |

Dynamic tensor shapes, general scalar/tensor control-flow mixing, vision
compression, self-hosting and bootable bare-metal deployment are not implemented.
AMX and AVX-512 code is guarded, but its arithmetic has not been executed
on supporting hardware in the recorded audit.

## Verify

```sh
make test-ast-tensor
make verify-local
```

Tests compare canonical IR, assembly text and complete ELF files with the
reference sidecar; native results are compared with an independent C oracle.
Failure cases cover invalid ASTs, exhausted registers, output errors and
no-overwrite behavior. Historical measurements and their exact boundaries
are in [Verification](docs/VERIFICATION.md); a hash proves identity, not quality.

CI is manual-only. No push or pull-request workflow is enabled on this branch.

## Tools

| Tool | Role |
| --- | --- |
| `niyahlex` | Tokens, optional morphology annotations and raw-word analysis |
| `niyahparse` | Syntax, AST display and scalar semantic checking |
| `niyahrun` | Scalar interpreter; displays the full signed result |
| `niyahc` | Unified scalar/tensor compiler; direct ELF publication |
| `niyahc-tile` | Reference sidecar for build-time templates and parity tests |

A generated scalar program returns main's result modulo 256 as its process
status. Tensor and scalar CLI error contracts are documented separately;
legacy scalar codes are preserved. See [Tensor AST](docs/TENSOR_AST.md).

## License

MIT. Included source provenance and license notices are retained.
