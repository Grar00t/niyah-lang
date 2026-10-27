# Unified tensor AST / native compiler

The production `niyahc` target is now a static Linux x86-64 executable with
no libc, ELF interpreter, NEEDED entries, unresolved symbols, or compiler
subprocesses. The scalar `niyahrun` interpreter and explicitly hosted legacy
test drivers are not converted or described as libc-free.

## Frontend and supported grammar

`A_TENSOR_OP` and its `Node_TensorOp` payload live in the core arena AST.
`tensor_input_a()`, `tensor_input_b()`, `tensor_gemm(a,b)`,
`tensor_emit(c)`, and `tensor_reserve(mask)` are reserved compiler intrinsics,
not external function calls. Tensor types are `i8x16x64`, `i8x64x16`, and
`i32x16x16`. A tensor program currently has exactly one parameterless
`رئيسية() -> صحيح`, straight-line declarations/intrinsics, and final
`أرجع 0;`. The tested example is `examples/tensor_intrinsics.niyah`.
The old top-level `input_a`, `input_b`, `gemm`, `emit`, `reserve` syntax is
parsed into the same arena as a compatibility spelling. It does not launch
the sidecar. Old scalar programs retain their language semantics and binary
emission. Arbitrary scalar/tensor mixing, branches, loops, multiple functions,
spills and dynamic shapes in tensor programs are deliberately rejected.

## Annotation, lowering and boundaries

Lexer Tokens preserve original UTF-8 byte spans and source positions.
An annotation pass runs the existing `morph_analyze`, stores each definition's
candidate count and root triples, and attaches the metadata-table index to
its AST intrinsic. Lowering then copies that metadata to the canonical
1024-byte value record, never using a root as symbol identity or embedding.
Names remain exact-byte symbols. Maximum name size is 128 bytes; truncated
morphology results fail closed. The context holds at most 64 values and 128
operations, with a 4096-node tensor AST bound and 1 MiB source bound.

For each value v, the interval is [definition, last-use + 1); an unused value
ends at definition + 1. The AST index is not the liveness index: reserve,
types, callee-name children and the final return do not consume tensor op
positions. Input and output lifetimes overlap at GEMM. Reserved mask M leaves
8-popcount(M) slots. A conflict needs one color per simultaneously live value.
The start-ordered allocator therefore succeeds exactly when peak liveness
fits those slots. Equal roots do not alias values. Allocation cost is O(8V),
with a separate O(V^2) returned-color check.

The allocator's proven coloring body is extracted unchanged as source at
compiler build time and surrounded by a checked, repeatable SysV function
ABI. Input is the NTILE001 byte buffer; output is the TMM1 byte buffer.
Neither uses text files, pipes or a child process. The original standalone
allocator binary remains byte-identical and serves as the differential
oracle. Its binary critical-block hash is NOT claimed for relocated library
instructions: RIP-relative displacements and error returns are different.
The callable allocator validates the full input before returning pressure,
and commits output only on success. This compiler's static lowering context
is single-threaded/non-reentrant; concurrent embedding requires separate
contexts, not concurrent calls to these entry points.

## Native emission and byte parity

The build-time generator invokes pinned sidecar/allocator and GNU as/ld to
establish the reference format and trusted instruction templates. The
production compiler contains instruction bytes, relocation descriptors,
runtime sections and a fixed symbol/name-table schema. Its bounded native
linker emits ET_EXEC directly and checks each signed32 displacement. It does
not invoke niyahc-tile, tile_allocator, as, ld, a shell, or Python at runtime.
This is a linker for the declared fixed three-section tensor backend, not a
general ELF linker for arbitrary input objects.

Both the complete `.s` text and the COMPLETE ELF file (not only .text) are
compared with the sidecar across all reserved masks and multi-page plans.
The demo is 11888 bytes, SHA256
460d3d202fd42d389ff03e7c6305c6c7296480752ff81bead47550187db3c0f4.
Changing the compiler implementation may change niyahc's own identity while
leaving identical input programs' complete output files unchanged.

The runtime and dispatcher source bytes are the pinned verified backend.
All tensor executions use AMX -> AVX512 -> AVX2 -> scalar policy, including
process-level AMX permission checks. No AMX or AVX512 arithmetic has been
hardware-verified on the audit i5-10400F. Unsupported explicit modes return77.
The fixed contraction is signed INT8 A16x64 * B64x16 -> INT32 C16x16;
|partial sum| <= 64*128*128 = 2^20 < 2^31. AVX2 uses exact integer arithmetic.
Morphology cannot establish a performance or language-quality improvement.

## CLI and output safety

    make niyahc
    ./niyahc -o /tmp/new-tensor.elf examples/tensor_intrinsics.niyah
    /tmp/new-tensor.elf features
    ./niyahc --emit-tensor-asm examples/tensor_intrinsics.niyah
    ./niyahc --emit-tensor-ir examples/tensor_intrinsics.niyah
    make test-ast-tensor

Binary publication writes a private mode0600 O_EXCL/O_NOFOLLOW temporary,
synchronizes it, and creates the destination with a no-replace hard link.
Existing files/symlinks are never opened for writing. Only the temporary
created by that invocation is unlinked. Parent directory fsync/crash-recovery
journaling is not claimed. Stdout emit modes report EPIPE and /dev/full as74.
Tensor semantic/allocation errors return65, pressure returns77, success0.
Read errors in explicit tensor-emission modes return66. Legacy scalar CLI
usage, parsing, semantic and general-emission codes remain1/2/3/4 for backward
compatibility. The generated runtime's own errors are unchanged.

AST dump support preserves intrinsic names/children. Tensor code generation
requires the original source span and is not supported from the old scalar
AST-deserialization format, which does not retain full original-source
coordinates. This fails closed rather than inventing morphology provenance.

## Verification scope

`tests/test_ast_tensor.py` compares canonical IR, assembly and complete ELF,
runs native scalar/auto/AVX2 against independent scalar C, checks all 256
masks and capacity boundaries, tests source/output errors and no-overwrite,
and runs tensor API canaries with ASan/UBSan. An isolated copy of niyahc
compiles with PATH=/nonexistent and no adjacent helper files. The optional
strace check runs only where strace is already installed; absence is not
reported as a tracing pass. Setting NIYAH_SCALAR_BASELINE supplies the
preserved prior niyahc for additional byte-for-byte legacy comparisons.
The test harness uses Python/hosted sanitizer libraries at verification time;
none is needed by the shipped compiler or emitted ELF. Linux/WSL remains
required: these files are not a bootable bare-metal operating system.
