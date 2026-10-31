# Core tensor intrinsics, spills, and the v2 runtime

`niyahc` parses tensor operations into the core `A_TENSOR_OP` AST and its
`Node_TensorOp` payload, lowers them to typed IR, and emits static Linux x86-64
ELF executables directly. Compilation and generated programs need no libc,
dynamic loader, compiler subprocess, or Python runtime. The executables use
Linux system calls and therefore require Linux or a compatible environment
such as WSL; they are not bootable operating-system images.

The implementation supports a bounded, straight-line tensor language. It has
two emission paths: the preserved v1 compatibility backend and the extended
v2 backend described here. The legacy `niyahc-tile` remains a build/test oracle;
the production compiler does not invoke it while compiling user programs.

## Build and run

From the language checkout, identify the existing assembly-model checkout:

```sh
make -C /absolute/path/to/niyah-assembly-model tile-allocator
make NIYAH_MODEL=/absolute/path/to/niyah-assembly-model niyahc
make NIYAH_MODEL=/absolute/path/to/niyah-assembly-model verify-local
```

The full `verify-local` gate includes the existing language checks and the new
tensor, runtime, schedule, and normalization checks. GNU make, the C compiler,
GNU `as`/`ld`, and Python are build or verification tools. Sanitizer libraries
are used by hosted verification drivers; they are not linked into production
`niyahc` or emitted tensor executables.

[tensor_extended.niyah](../examples/tensor_extended.niyah) applies all four
arithmetic intrinsics. The following example supplies two all-one INT8 matrices
and reads its one INT32 result. Use a fresh directory because compiler output
publication intentionally refuses to overwrite an existing destination.

```sh
tensor_demo_dir=$(mktemp -d)
./niyahc -o "$tensor_demo_dir/extended.elf" examples/tensor_extended.niyah
python3 -c 'import sys; sys.stdout.buffer.write(bytes([1])*2048)' > "$tensor_demo_dir/input.bin"
"$tensor_demo_dir/extended.elf" auto < "$tensor_demo_dir/input.bin" > "$tensor_demo_dir/output.bin"
python3 - "$tensor_demo_dir/output.bin" <<'PY'
import pathlib, struct, sys
values = struct.unpack('<256i', pathlib.Path(sys.argv[1]).read_bytes())
assert values == (128,) * 256
print(values[:16])
PY
```

This input gives a MATMUL result of 64 at every position. ADD doubles it to 128;
TRANSPOSE and RELU retain those values. The verification suite also uses varied,
negative, boundary, and overflow cases to distinguish the operations.

The compiler can export the lowered representation or assembly:

```sh
./niyahc --emit-tensor-ir examples/tensor_extended.niyah > "$tensor_demo_dir/extended.nir"
./niyahc --emit-tensor-asm examples/tensor_extended.niyah > "$tensor_demo_dir/extended.s"
```

The `.nir` output is binary. The `.s` output is readable assembly and includes
the v2 graph color preferences, spill count, and physical eviction count.

## Types and operation semantics

Every runtime value occupies exactly 1,024 bytes. The supported types are:

| Language type | Logical contents | External memory layout |
| --- | --- | --- |
| `i8x16x64` | Signed INT8 A, 16 rows × 64 columns | Row-major, 64 bytes per row |
| `i8x64x16` | Signed INT8 B, 64 rows × 16 columns | Row-major, 16 bytes per row |
| `i32x16x16` | Signed INT32 C, 16 rows × 16 columns | Row-major, 64 bytes per row; little-endian words |

| Intrinsic | Arguments | Result and behavior |
| --- | --- | --- |
| `tensor_input_a()` | None | The next input block, typed as A |
| `tensor_input_b()` | None | The next input block, typed as B |
| `tensor_matmul(a,b)` | A and B | C[i,j] = sum over k=0..63 of A[i,k]×B[k,j]; selects v2 |
| `tensor_gemm(a,b)` | A and B | Same mathematics; retains v1 when the program meets compatibility conditions |
| `tensor_add(x,y)` | Two C values | Elementwise addition modulo 2^32, interpreted as signed INT32 afterward |
| `tensor_transpose(x)` | One C value | C[i,j] = x[j,i] |
| `tensor_relu(x)` | One C value | Signed INT32 max(x[i,j],0) |
| `tensor_emit(x)` | One C value | Append its 1,024 bytes to the output stream |
| `tensor_reserve(mask)` | Integer 0..255 | Reserve physical TMM registers; at most once, before tensor operations |

MATMUL starts from a zero accumulator. It does not implicitly accumulate a
previous result. A 64-term signed INT8 dot product has absolute value at most
64 × 128 × 128 = 1,048,576, so its INT32 result is exact. ADD deliberately wraps;
RELU tests the sign of the resulting INT32 value, including after overflow.
TRANSPOSE, ADD, and RELU currently accept C values only. There is no arbitrary
shape inference, automatic broadcasting, quantization, or conversion of a C
value back into an A/B input.

A normal tensor program contains one parameterless `رئيسية() -> صحيح`,
straight-line initialized tensor declarations and intrinsic statements, and a
final `أرجع 0;`. Both `متغير` and `ثابت` declarations introduce a single tensor
definition; reassignment and forward references are rejected. Arbitrary
scalar/tensor mixing, branches, loops, multiple functions, and dynamic shapes
are unsupported.

The top-level compatibility spelling is also accepted:

```text
input_a a;
input_b b;
matmul product = a * b;
add sum = product + product;
transpose transposed = sum;
relu activated = transposed;
emit activated;
```

This spelling enters the same core AST. It is not a sidecar invocation.

## Input, output, and backend selection

Generated programs read exactly 1,024 bytes for every input declaration, in
declaration order. There is no input header. After the expected bytes, stdin
must reach EOF; extra bytes or early EOF are invalid. Each `tensor_emit` adds
one row-major, little-endian INT32 matrix to stdout in statement order, without
a header. No floating-point encoding is involved.

V2 accepts zero arguments or one of `auto`, `avx2`, `amx`, `scalar`, and
`features`. Zero arguments means `auto`.

- `auto` attempts AMX after the required CPU, OS-state, geometry, and process
  permission checks. Otherwise it uses AVX2. If neither backend is usable, it
  exits 77 before executing an unsupported instruction.
- `avx2` and `amx` request that backend explicitly. Unsupported requests exit 77;
  they do not bypass feature or permission checks.
- `scalar` is an explicit verification mode. It does not participate in v2
  automatic fallback.
- `features` returns 64 binary bytes describing capabilities and the automatic
  selection, without reading matrix input. It remains available when no
  automatic backend is usable.

The eight little-endian 64-bit feature fields are: magic `NXISA001`, capability
bits, XCR0, CPUID.1.ECX, CPUID.7.0.EBX, CPUID.7.0.EDX, selected backend, and AMX
permission status. Capability bits 0/1/2 describe AVX2/AVX512F/AMX eligibility
from the detector. Selection IDs 0/1/2/3 describe scalar/AVX2/AVX512/AMX; the v2
automatic policy does not select AVX512. Selection value 77 reports that no
automatic backend is available. A detected capability alone is
not proof that the corresponding arithmetic ran.

The AMX path requests Linux `arch_prctl(ARCH_REQ_XCOMP_PERM,18)`: the second
argument is component index 18, not the bitmask `1<<18`. Permission and supported
state are checked before tile instructions. The v2 geometry check ensures the
implementation can support its 8 tiles, 16 rows, and 64-byte rows. The 64-byte
configuration uses palette 1, start row 0, and zero reserved fields.

AVX2 GEMM uses `vpmaddubsw`, `vpmaddwd`, and `vpaddd` with an exact signed-byte
decomposition. For a source byte, its signed value is `(bits & 127) - (bits & 128)`.
The two separate pair sums stay within signed 16-bit range before widening, so
the saturating instruction does not discard information. The difference is
accumulated in INT32. This preserves the INT8 arithmetic contract at the signed
endpoints as well as for typical values.

## Lifetimes, graph coloring, and spills

For each value v, let d(v) be its definition's tensor instruction index and
U(v) its use indices. The interval is:

```text
I(v) = [d(v), 1 + max({d(v)} union U(v)))
```

An unused definition lasts through its defining instruction. Inputs and result
overlap at a MATMUL instruction; the result cannot steal a source register
before the source is read. Reserve statements, type syntax, and final return
do not consume tensor instruction positions.

V2 independently validates definitions, operand types, counts, and these
intervals. It constructs a 64-vertex bitset interference graph, adding an edge
when two half-open intervals overlap. Definitions are processed in start order
with deterministic lowest-color selection. Reserved registers are excluded.
When no color is available, that value is marked as spilled rather than
rejecting the whole program.

Graph colors are preferences for the physical scheduler. Actual residency can
change while a value is live. The scheduler pins both GEMM inputs, acquires a
third distinct register for the destination, and evicts only unpinned values.
Every displaced live tile is stored with `TILESTORED`; a needed nonresident
input is brought back with `TILELOADD`. There is no permanently assumed scratch
tile that might overwrite a live operand. GEMM needs at least three unreserved
registers, so a mask leaving fewer than three is rejected with 77 even though
stack homes exist.

Each virtual value has its own 1,024-byte home at `r12 + value_id*1024`.
The frame base is aligned down to 64 bytes and the original stack pointer is
restored on return. A 64-byte alignment requirement does not reduce a full
tile's storage size to 64 bytes. At most 64 homes consume 65,536 bytes, plus
alignment and call-frame overhead.

The vector path keeps B row-major in its home. The AMX path packs B as
`P[g,4*j+l] = B[4*g+l,j]` before loading it. Both physical tile geometry and
spill/reload width remain 16 × 64 bytes. A result produced in a tile is written
to its home before ADD, TRANSPOSE, RELU, or memory-based emission needs it.
Those C operations use memory helpers and preserve live tile contents.
`LDTILECFG` is issued once before the scheduled tile region; it is not used to
change an individual live tile's shape. `TILERELEASE` ends the region.

[tensor_spills.niyah](../examples/tensor_spills.niyah) defines twelve MATMUL
results before emitting any of them. With its two shared inputs, the last
MATMUL has 14 simultaneously live intervals. The compiler therefore requires
v2 even with the normal eight-register mask. It needs 2,048 input bytes and emits
12,288 output bytes. Setting `tensor_reserve(248);` before the input
declarations leaves `tmm0`–`tmm2` available and further exercises eviction.

## Identifier normalization and morphology

Tensor symbol keys are normalized in this order:

1. Full Unicode NFKC, using the Unicode version supplied by the build's Python
   Unicode database.
2. Alif unification: U+0622 `آ`, U+0623 `أ`, U+0625 `إ`, and U+0671 `ٱ` map to
   U+0627 `ا`.
3. Removal of Unicode category M marks, including Mn, Mc, and Me.

The native normalizer has generated decomposition, combining-class,
composition, and mark tables, plus algorithmic Hangul handling. It does not
load Unicode data files or call Python while compiling. The generated table
version and SHA-256 are recorded in `build/tensor_core/unicode_manifest.json`;
retain that information when reproducing a build.

Raw identifier tokens are bounded to 128 UTF-8 bytes. Normalized output is
bounded to 512 bytes; malformed UTF-8, overflow, and empty normalized symbols
are rejected. The integrated core lexer additionally accepts the classified
Arabic presentation letters and Arabic marks needed by this normalization.
That extra classifier is guarded by the core build macro. It does not make
every Unicode character legal in the source language or alter the pinned
sidecar's lexer.

Definitions and uses resolve through normalized keys. Two definitions that
normalize to the same key are an error, including visually different Alif or
diacritic spellings. For example, `أثر` and `اثر` cannot define separate tensor
values in one program. Original UTF-8 names, source spans, and positions remain
in the AST/IR as provenance.

Morphology runs on the normalized key and retains candidate counts and root
triples as annotations. Roots do not determine symbol identity: two different
normalized words sharing an Arabic root remain different symbols. This work
has not measured an 18% token-fertility reduction or a language-quality gain.
Lexical normalization is implemented; those statistical outcomes would require
a separately defined corpus, tokenizer, baseline, and measurement.

## Bounds and failure contracts

The supported compilation bounds are 64 SSA values, 128 tensor operations,
4,096 AST nodes, and 1 MiB of source. Input/output arrays and code regions are
also bounded. Every generated program contains only the instructions for its
validated fixed plan. The compiler uses a single static context and is not
reentrant; embedding code must serialize calls or use separate process/context
instances.

| Code | Production compiler | Generated v2 runtime |
| --- | --- | --- |
| 0 | Successful emission/publication | Successful matrix execution or feature report |
| 64 | Invalid command-line arguments | Invalid runtime mode/argument count |
| 65 | Invalid syntax, AST, IR, types, or normalized symbol collision | Invalid fixed plan or malformed input length |
| 66 | Source file open/read/close failure | Input read system-call failure |
| 74 | Output emission, write, or no-replace publication failure | Output write failure, including broken pipe |
| 77 | Resource bound exceeded or impossible physical operand allocation | Requested/automatic hardware execution cannot use a supported backend |

The compiler checks output capacity and stages v2 code/assembly privately before
copying it to the caller, so a rejected emission does not partially modify the
API output buffer. CLI publication uses an exclusive private temporary,
synchronization, and a no-replace hard link. Existing files and symlinks are not
opened for writing. Parent-directory crash-recovery journaling is not claimed.
An output stream that fails after a partial write cannot generally retract
bytes already delivered.

These production compiler codes also apply when compiling scalar source.
Explicitly hosted legacy scalar verification drivers retain their historical
contracts. A generated scalar program retains its own return value and traps;
its application result is not remapped to a compiler status.

## V1 compatibility and the scope of parity

Programs using only original `tensor_gemm`/`gemm` operations, unchanged identifier keys, and
register pressure that fits the available tiles retain the v1 path described
in [TENSOR_AST.md](TENSOR_AST.md). The new `tensor_matmul`/`matmul` spelling
selects v2 even for a simple three-value contraction. It has the same
mathematical meaning as GEMM, with the current runtime and AVX2 instruction
path. ADD, TRANSPOSE, RELU, changed normalized spellings, or spills also select v2.
The emitted IR magic distinguishes `NIRG0001` and `NIRG0002`.

Both versions retain the 32-byte IR header, 1,024-byte metadata record per value,
16-byte operation record, and original source bytes. The IR metadata record
and the runtime 1,024-byte tensor home are different objects. New operation
codes are 6=ADD, 7=TRANSPOSE, 8=RELU; the original codes retain their meanings.

The historical v1 demo remains 11,888 bytes with SHA-256:

```text
460d3d202fd42d389ff03e7c6305c6c7296480752ff81bead47550187db3c0f4
```

This is whole-ELF parity, including code, layout, and metadata, for the preserved
fixture. Existing v1-compatible programs are compared against the legacy
pipeline. The v2 operations and spills were not expressible by that pipeline;
they are verified against independent numerical references and schedule tests.
There is no claim that a new v2 executable has the same bytes as a v1 executable.

Preserving v1 bytes also preserves its generated runtime's historical policy:
AMX → AVX512 → AVX2 → scalar automatic selection, and exit 65 for both negative
input reads and invalid input lengths. Those facts do not describe the strict
v2 automatic policy or v2 read-error status. The production compiler itself
uses the strict codes above for both paths.

The expected allocator digest
`59447d2ededb37fb66985be7cbb2bb3c4e7b8326443141b61880ece2158e0866`
identifies the standalone `build/tile_allocator` executable, not its `.s`
source. Source identity and executable identity must be checked separately.
The pinned sidecar digest is
`b6924af76ea75dd3f23ef80ff926d67f74959185c346ec178c8df03ad543afe8`.
Hashes establish the identity of those byte sequences; functional tests establish
the tested behavior.

The v1 build generator now writes
`build/tensor_core/niyah_legacy_tensor_templates.h`, and the v2 generator writes
`build/tensor_core/tensor_v2_templates.h`. The distinct v1 name prevents a stale
`src/tensor_templates.h` from taking precedence over the regenerated build
header. Instruction encodings are assembled at build time, with bounded
relocation descriptions embedded in the compiler. Production emission uses
those bytes directly and never invokes `as` or `ld`.

## Verification evidence and source map

The verification categories include legacy IR/assembly/whole-ELF parity,
numerical comparisons for every new operation, signed INT8 endpoints, INT32
wrapping, reserved-register masks, pressure-driven spill/reload, packing,
malformed AST/IR, output canaries, I/O status codes, deterministic rebuilds,
Unicode normalization/collisions, and compilation with no adjacent helpers or
usable subprocess PATH. ELF checks inspect dynamic dependencies and executable
segment permissions. Use the generated test receipts for the exact commands,
counts, identities, and machine observations of a particular run.

AMX arithmetic has not been hardware-verified on the audit host. The AMX
schedule interpreter checks the emitted load/store/packing/accumulation
sequence, including spills, against independent results; it is a simulation,
not native AMX execution. The schedule test can explicitly execute AMX when a
test host provides and permits it, and records that separately. Feature
detection, successful compilation, or a disassembly containing AMX instructions
does not establish native AMX execution.

| Source | Responsibility |
| --- | --- |
| [parser.h](../src/parser.h), [parser.c](../src/parser.c) | Core tensor AST and intrinsic parsing |
| [tensor_core.c](../src/tensor_core.c) | AST validation, normalized names, typed lowering, v1/v2 routing |
| [tensor_v2.c](../src/tensor_v2.c) | Explicit interference graph, physical schedule, native ELF and assembly emission |
| [tensor_v2_runtime.s](../src/tensor_v2_runtime.s) | Syscalls, backend policy, geometry checks, exact arithmetic helpers |
| [tensor_normalize.c](../src/tensor_normalize.c) | Native Unicode normalization and extra Arabic classification |
| [gen_tensor_v2.py](../tools/gen_tensor_v2.py) | Build-time v2 ELF/instruction templates |
| [gen_tensor_unicode.py](../tools/gen_tensor_unicode.py) | Build-time Unicode tables and manifest |

Hardware/OS references used for the implementation:

- [Linux x86 extended-state permission contract](https://docs.kernel.org/arch/x86/xstate.html).
- [Intel architecture reference: tile configuration and AMX exception classes](https://cdrdv2-public.intel.com/843860/architecture-instruction-set-extensions-programming-reference-dec-24.pdf).
- [Intel SDM Volume 2B: INT8 tile dot-product semantics](https://cdrdv2-public.intel.com/782151/253667-sdm-vol-2b.pdf).
