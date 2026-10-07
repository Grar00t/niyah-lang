# Research Synthesis — 2026-10-07

## Papers read for this repository

- **Onramp: A portable self-bootstrapping C compiler** — primary source: `ludocode/onramp`. Mechanism: machine-code/hex seed -> small VM -> linker -> assembler -> minimal C -> progressively larger C subsets. Current primary source states the project is still under construction and uses a custom bytecode VM after the first platform-specific stages.
- **nCPU: Self-Hosting C Compiler on Metal GPU** — primary source: `robertcprice/nCPU`, paper in the repository. Current paper text describes `cc.c` as 3,461 lines, compiled first with `aarch64-elf-gcc -O2`, then executed as ARM64 on a Metal-backed virtual CPU. The supplied ~4,200-line value is not current in that paper. Deterministic cycle-count measurements do not by themselves establish immunity to timing side channels.

## Changes committed

- None. The repository already specifies a staged bootstrap to direct x86-64 output; adding Onramp's custom VM would introduce a second execution architecture rather than close the current parser/typechecker/codegen gap. nCPU requires Metal, ARM64, and a host cross-compiler, violating the repository constraints.

## Papers read but rejected

- **Onramp** — rejected for implementation: its core portability mechanism is a custom bytecode VM; Niyah's existing architecture targets direct x86-64 ELF/static-syscall output, and replacing that target is not justified by a current defect.
- **nCPU** — rejected for implementation: requires Apple Metal/GPU execution and ARM64 bootstrap tooling outside the allowed C/x86-64-only implementation target; the timing-side-channel immunity claim is unverified.

## Papers requiring operator decision

- None.
