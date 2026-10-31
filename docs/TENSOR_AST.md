# Fixed-shape tensors in the primary niyahc compiler

This page describes the bounded production contract and the commands that
exercise it. Current receipts and measured artifacts determine verification
status; this page does not inherit a passing status from an older build.

## Supported frontend

`A_TENSOR_OP` and `Node_TensorOp` are part of the existing arena AST.
The production subset has five intrinsics:

| Intrinsic | Operands and result |
| --- | --- |
| `tensor_input_a()` | Defines signed `i8x16x64` A |
| `tensor_input_b()` | Defines signed `i8x64x16` logical row-major B |
| `tensor_gemm(a, b)` | Defines zero-initialized `i32x16x16` C = A B |
| `tensor_emit(c)` | Emits the complete INT32 result |
| `tensor_reserve(mask)` | Reserves tile colors selected by an integer mask in 0..255 |

`tensor_matmul(a, b)` is an alias for the same GEMM operation. It does not
select another backend or enable spills. Compatibility spellings `input_a`,
`input_b`, `gemm` / `matmul`, `emit`, and `reserve` lower into the same AST.

A native-syntax tensor program has one parameterless
`رئيسية() -> صحيح`, straight-line typed declarations and intrinsics,
and a final `أرجع 0;`. The Arabic example is
[examples/tensor_intrinsics.niyah](../examples/tensor_intrinsics.niyah).
Dynamic shapes, tensor control flow, arbitrary scalar/tensor mixing and spills
are rejected. The experimental ADD, TRANSPOSE and RELU operations are outside
this production subset.

Intrinsic call names are compiler-resolved. Definitions inside a tensor
program may not shadow those names. Ordinary scalar declarations that do not
use tensor intrinsics retain their historical binding behavior; reserving
tensor spellings must not silently invalidate an otherwise scalar program.

## Identity, provenance and typed values

Identifier binding compares exact original UTF-8 bytes. Alif variants,
diacritics and identifiers sharing a morphology root remain distinct.
Morphology candidates are bounded provenance annotations, never embeddings,
register numbers or a substitute for lexical identity. Invalid UTF-8 and
invalid source spans are rejected. Raw names are bounded to 128 bytes;
truncated morphology results fail closed.

The AST retains its original tokens and source coordinates. After lowering,
each tensor node also carries a zero-based value ID and type, zero-based
operand IDs, a bounded metadata reference and its derived lifetime. A metadata
reference is value ID + 1; zero means no reference.
`NIYAH_TENSOR_NO_VALUE` marks absent value/operand IDs. Reserve creates no
value; emit refers to its operand rather than defining another value.

The current context supports at most 64 values, 128 tensor instructions,
4096 AST nodes and 1 MiB of source. Value records retain element/shape type,
layout contract, definition/end indices, spelling, position and morphology
candidates. Tensor AST deserialization through the historical scalar text
format is explicitly rejected because that format cannot recover the original
source provenance. Scalar serialization remains a separate supported path.

## Lowering and allocation

The pass order is AST validation, annotation, typed definitions/uses,
instruction numbering, lifetime derivation, allocation, independent allocation
validation, then emission. Types, callee-name AST nodes, reserve and the final
return do not create tensor instruction positions.

For each value, the half-open lifetime is:

\[
I(v)=[d(v),e(v)),\qquad
e(v)=1+\max(\{d(v)\}\cup U(v)).
\]

An unused value therefore has lifetime `[d,d+1)`. A GEMM result and both
operands are live at the GEMM instruction, so the result cannot overwrite
either operand during that instruction.

For the example with `tensor_reserve(1)`:

| Instruction | Operation | Value | Lifetime | Color |
| ---: | --- | ---: | --- | ---: |
| 0 | Input A | 0 | [0,3) | 1 |
| 1 | Input B | 1 | [1,3) | 2 |
| 2 | GEMM(A,B) | 2 | [2,4) | 3 |
| 3 | Emit value 2 | reference to 2 | no new value | 3 |

This is a contract trace, not a hardware execution claim. Color 0 is reserved.
The allocator processes already start-ordered intervals, keeps input order
for equal starts, and chooses the lowest free unreserved color. A color is
reusable when its previous end is no greater than the new start.

With eight uniform compatible colors and one contiguous interval per value,
allocation succeeds exactly when peak overlap fits
`8 - popcount(reserved_mask)`. The ordered sweep costs O(8V); the independent
pairwise validation costs O(V^2). This claim does not extend to arbitrary
control-flow interference or per-value register restrictions.

The checked in-memory interface is:

- Input: `NTILE001`, U8 count in 1..64, U8 reserved mask, six zero bytes,
  then count pairs of little-endian U32 begin/end. Exact length is `16+8N`.
- Output: `TMM1`, U8 count, three zero bytes, then count color bytes.
  Exact successful length is `8+N`.
- Invalid input returns 65; pressure returns 77; success returns 0.
  The callable interface returns to its caller and publishes no allocation
  bytes on rejection.

The consumer independently checks the result header, count, reserved bytes,
color range, reserved colors and every interfering pair. Pressure is rejected
with 77 across modes; it does not activate a spill scheduler. TMM colors are
AMX resources. Scalar and AVX2 use explicit 1024-byte memory slots and separate
machine temporaries.

The coloring body is extracted from the standalone allocator at build time
and wrapped in a checked SysV ABI. Its relocated instructions have their own
identity; the standalone binary's hash is not a hash of the callable copy.
The compiler and allocator currently use static mutable contexts and are not
reentrant. Concurrent calls to these entry points are unsupported.

## Arithmetic and guarded dispatch

A is signed INT8 16x64, logical B is signed INT8 64x16, and C is signed INT32
16x16, initially zero. Each occupies 1024 bytes; packed B also occupies 1024
bytes. The packing contract is:

\[
B_{\mathrm{packed}}[g,4j+\ell]=B[4g+\ell,j].
\]

For a zero accumulator and at most 64 products, every partial sum satisfies
`abs(sum) <= 64 * 128 * 128 = 2^20 < 2^31`. Integer implementations must preserve
signed-byte multiplication and exact INT32 accumulation.

Automatic dispatch prefers usable AMX-INT8, then AVX512, AVX2 and Scalar.
XGETBV is guarded by XSAVE and OSXSAVE. The dispatcher checks required XCR0
state, AMX palette/TMUL limits, and supported/permitted tile state through raw
Linux arch_prctl calls in the generated process. AMX eligibility is independent
of AVX512 eligibility. Explicit unavailable backends return 77.

The corrected dispatcher and runtime differ from the historical reference:
they validate palette/TMUL limits and distinguish raw input read failures
from malformed input. No AMX or AVX512 hardware arithmetic verification is
claimed for the i5-10400F environment. Assembly checks and mocked feature
snapshots are separate evidence and cannot establish hardware arithmetic.

## Native emission and reference identities

Compilation parses, lowers, allocates, relocates and emits in process.
Production niyahc does not invoke niyahc-tile, tile_allocator, as, ld, Python
or a shell while compiling a user program. Build-time template generation can
use those tools. The bounded emitter constructs a static Linux x86-64 ET_EXEC
and checks relocation ranges; it is not a general-purpose ELF linker.

The verification workspace observed these unchanged oracle identities:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Standalone tile_allocator | 9176 | `59447d2ededb37fb66985be7cbb2bb3c4e7b8326443141b61880ece2158e0866` |
| Standalone niyahc-tile | 30712 | `b6924af76ea75dd3f23ef80ff926d67f74959185c346ec178c8df03ad543afe8` |

The demo references are deliberately distinguished:

| Reference | Bytes | SHA-256 |
| --- | ---: | --- |
| Preserved historical fixture | 11888 | `460d3d202fd42d389ff03e7c6305c6c7296480752ff81bead47550187db3c0f4` |
| Measured corrected source reference, build/tensor_core/reference.elf | 12040 | `1e85b97a1ea375b402fd1890195c07430bad9e02a4136378d7a0a48c64c412d1` |

The corrected artifact does not match the historical whole-file hash.
Current parity checks compare canonical typed IR, complete assembly text and
complete ELF files produced through both routes with the same corrected
backend sources and toolchain. Outputs are not stripped or normalized after
a mismatch. Arithmetic is checked separately against independent references.
niyahc's own size/hash is a separate measurement from any emitted program.

## CLI compatibility and publication

Generic scalar invocation preserves historical compiler exit categories:

| Exit | Generic scalar invocation |
| ---: | --- |
| 0 | Successful compilation |
| 1 | CLI, source-I/O or output-publication failure |
| 2 | Parse failure |
| 3 | Semantic-check failure |
| 4 | Native-code-generation failure |

`--tensor`, `--emit-tensor-ir` and `--emit-tensor-asm` select the tensor exit
contract before the source can be read. Without an explicit tensor option,
tensor handling is inferred once parsing identifies tensor syntax; an
unreadable file cannot be classified by contents. Use `--tensor` when the
tensor exit contract is required even for early CLI or source-I/O errors.

| Exit | Tensor interface and generated tensor programs |
| ---: | --- |
| 0 | Successful requested operation |
| 64 | Invalid CLI usage or unknown runtime backend selector |
| 65 | Invalid syntax, AST, type, IR, allocation or input byte structure |
| 66 | Source/input open or raw read failure |
| 74 | Output/write/publication failure |
| 77 | Unsupported resource demand or explicit unavailable backend |

Existing destinations and destination symlinks are not overwritten.
Publication uses an exclusive private temporary and no-replace linking;
only a complete output is published. Writes handle EINTR and short writes,
and output failures include EPIPE and /dev/full. Parent-directory fsync and
crash-recovery journaling are not claimed.

## Run the Arabic demonstration from any directory

In Bash on Linux/WSL:

```sh
python3 /home/a/niyah-lang-integrated-20261027/tools/run_tensor_demo.py --model /home/a/niyah-assembly-model
```

The script resolves its compiler root from its own path, runs `make niyahc`
there with the selected backend source root, and compiles
`examples/tensor_intrinsics.niyah` directly through primary `niyahc --tensor`.
No separate helper rebuild is required from the caller.

The executable is published into a newly created directory under `build`;
the report prints its path and leaves it available for inspection. The script
checks all result elements for uniform A=1/B=2 (every C element must be 128),
then a signed nonuniform fixture against Python integer sums using logical B
indices. It runs Scalar and automatic selection, plus AVX2 when usable.
An unavailable explicit AVX2 request must return 77 and is recorded as
NOT_RUN for arithmetic.

The script prints executed case/value counts, exit codes, byte lengths and
SHA-256 values only after the actual comparisons succeed. It returns zero
only after successful build, compilation and all required comparisons.
This small demonstration is not the complete acceptance suite, a benchmark,
or evidence of an improved language model.

## Dependencies, experimental code and verification limits

Build/demo dependencies are Python 3, GNU make with grouped-target support,
a C compiler, GNU assembler/linker and the existing source repositories.
Some verification gates additionally use hosted C, sanitizer runtimes and
binary-inspection tools. These are separate from production runtime
dependencies. No package installation is part of the demo.

The production compiler and generated executables are required to use raw
Linux x86-64 syscalls without libc, libm, a dynamic loader or runtime helpers.
Static linking by itself does not prove that requirement: inspect ELF/program
headers, dynamic entries, symbols, disassembly and link inputs/map.
Linux/WSL remains a dependency; these ELF files are not a bootable OS.

[TENSOR_V2.md](TENSOR_V2.md) and V2/normalizer source files are retained as
experimental material. ADD/TRANSPOSE/RELU, normalized binding and spills
described there are not this production contract. The V2 acceptance target
is outside default `make test` and is not proof of the current compiler's
supported behavior. Standalone normalization-analysis tests do not alter
identifier binding.

The active gates are the scalar regressions, AST/IR/assembly/whole-ELF parity
checks, callable-allocation and ABI checks, guarded runtime arithmetic checks,
and actual syscall error paths. Test counts and outcomes belong in generated
receipts after execution. Hosted sanitizers do not instrument all generated
or assembly machine code; guard pages and separate ABI checks supplement them.
