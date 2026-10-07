# Niyah architecture

Single binary, whole-program, one translation unit per run. Input: `.نيّة` files. Output: static ELF64 ET_EXEC. No `as`, no `ld`, no libc.

## Pipeline

```
source bytes -> lexer -> parser -> AST -> typecheck -> IR (3AC, CFG) -> regalloc -> x86-64 encoder -> linker pass -> ELF
```

| Stage | Input | Output | Data structure | Notes |
|---|---|---|---|---|
| Lexer | UTF-8 bytes | Token stream (pull, 1-token lookahead) | `Token{kind,line,col,start,len,value}` | Slices into source buffer; no copies. Keywords matched bytewise. |
| Parser | tokens | AST | Recursive descent; precedence climbing for binary ops | LL(1). No backtracking. Error recovery: sync on `;` / `}`. |
| AST | | | Arena-allocated tagged nodes, `u32` indices not pointers | Pointer-free so the same layout is expressible in stage1 Niyah. |
| Type checker | AST | Typed AST + symbol tables | Scope stack, struct table | Static, strict, no implicit coercion. Every conversion is `T(expr)`. Sizes and field offsets computed here. |
| IR | typed AST | per-function CFG of basic blocks, 3AC | `Inst{op,dst,a,b,type}` in a flat array; blocks hold index ranges | Non-SSA, virtual registers, explicit loads/stores for address-taken vars. |
| Codegen | IR | machine code bytes + fixups | Direct encoder (REX/ModRM/SIB), linear-scan regalloc | No text assembly stage. Calls use the System V integer ABI subset (rdi,rsi,rdx,rcx,r8,r9; rax ret). |
| Linker pass | code, data, fixups | ELF64 | Symbol table of {name, section, offset}; fixup list {site, symbol, kind} | Resolves rel32 call/jmp/RIP-relative; lays out `.text` (R+X) and `.data/.bss` (RW) as two PT_LOAD segments; writes `e_entry` to `_start`. |

Runtime: `_start` is emitted by the compiler (assembly stub, 6 instructions): align stack, call `رئيسية`, `exit_group(rax)`. System access is one intrinsic, `نداء_نظام(n,a,b,c,d,e,f)`, lowered to `syscall` with the kernel register convention (rdi,rsi,rdx,r10,r8,r9).

## Decisions

**Linker pass: emit ELF directly vs. emit `.s` for an external assembler.** Direct emission removes `as`/`ld` from the trusted base and is required for a zero-dependency bootstrap; cost is a hand-written encoder (~600 lines for the instruction subset used). Chosen: direct emission. The encoder only needs ~25 instruction forms.

**Source encoding: bytewise identifiers vs. NFC-normalised.** Normalisation needs Unicode tables (data dependency, large). Bytewise compare is deterministic and 10 lines. Chosen: bytewise; the same visual identifier typed with different code point sequences is two identifiers. Mitigation: lexer rejects nothing, a later lint pass may warn on U+0640 / presentation forms.

**Keyword set.** The seven specified keywords are insufficient for self-hosting (the compiler needs aggregates). `هيكل` (struct) is added as an eighth keyword. Everything else (types, `صواب`/`خطأ`, intrinsics) is predeclared identifiers in the root scope.

## IR selection: SSA vs three-address vs stack

| Property | SSA (LLVM IR, Cranelift CLIF, Go `cmd/compile/internal/ssa`) | Three-address, mutable vregs (GCC GIMPLE pre-SSA, Lua 5 register bytecode, lcc) | Stack (JVM bytecode, WebAssembly, chibicc-style push/pop emission) |
|---|---|---|---|
| Construction | Needs dominator tree, dominance frontiers, phi placement, renaming (Cytron et al.) or on-the-fly Braun et al. construction | Direct AST walk; one dst per instruction, variables are vregs assigned many times | Direct AST post-order walk, trivial |
| Optimisation | Best: GVN, DCE, constant prop are single-pass over def-use | Needs reaching-defs / liveness dataflow for the same | Poor: must be lifted to registers first |
| Register allocation | Needs phi elimination (parallel copies, critical-edge splitting) before or during allocation | Linear scan over live intervals; straightforward | Not needed for naive output; every value goes through memory/stack, ~3-5x more instructions |
| Code size in the compiler | Largest. Roughly 2-3x the middle-end of 3AC | Medium | Smallest |
| Self-hosting burden | Stage1 Niyah must express dominators, phi lists, use-lists: needs growable arrays, hash maps, worklists in a language that has only structs, pointers, arrays | Needs growable arrays and bitsets | Needs almost nothing |
| Output quality | Good | Adequate: values live in regs between ops | Low |
| Determinism for fixed point | Hash/worklist order must be pinned | Easy | Easy |

**Commitment: three-address code, non-SSA, with basic blocks and virtual registers; linear-scan allocation.**

Justification: the objective is a verifiable fixed point (`niyah2 == niyah3` byte-for-byte), not peak code quality. The compiler must be written in Niyah, so every data structure the middle-end needs becomes a feature stage0 must compile. SSA adds dominators, phis and parallel-copy lowering, which is the largest single chunk of non-essential code. A stack IR is smaller but forces either stack-machine codegen (chibicc-style, every temp via push/pop, 3-5x instruction bloat that also inflates the self-compile time and test surface) or a later stack-to-register lift, which is more work than building 3AC directly. 3AC keeps temporaries in vregs, gives linear scan something real to allocate, and leaves a clean upgrade path: converting the CFG to SSA later is a pass on top of this IR (Braun et al. construction works on exactly this representation) and does not touch the front end or the encoder.

## Bootstrap chain

| Stage | Binary | Built by | Source |
|---|---|---|---|
| 0 | `niyah0` | host `cc` (`make niyah0`) | `src/*.c` (C11, freestanding) |
| 1 | `niyah1` | `niyah0` | `compiler/*.نيّة` |
| 2 | `niyah2` | `niyah1` | `compiler/*.نيّة` |
| 3 | `niyah3` | `niyah2` | `compiler/*.نيّة` |

Self-hosting criterion: `cmp niyah2 niyah3` exits 0. `niyah1` may differ from `niyah2` (C-generated vs Niyah-generated code paths in the compiler itself do not matter; the emitted code for the same input must, so `niyah1` and `niyah2` are also required to emit identical output for every file in `tests/`). The host C compiler is trusted only for stage 0; after stage 2, `src/*.c` is frozen as the seed and never required again except to reproduce the chain from scratch.
