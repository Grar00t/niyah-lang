# M1 round-trip regression: allocation order versus tree order

Date: 2026-10-27
Scope: `niyah-lang` M1, unrelated to `Niyah.Engine`.

## Observed regression

The in-progress M1C3 test reported 6/7 `tree mismatch` failures,
while every serialized text re-serialization matched. The regression
was independently reproduced on Kali WSL from the preserved dirty files.

## Cause

`trees_match` used numeric arena positions `nodes[i]` and compared
`first/last/next` values directly. The parser sometimes allocates
children first (e.g., type before variable or binary operands before
operator), so the original arena allocation order is not preorder.
The deserializer allocates nodes in preorder. These are equivalent
tree shapes with different node indices.

## Repair

Compare node pairs by recursively/iteratively traversing *ordered*
children; inspect node kind, operator token kind, literal bytes and
numeric value; validate that all nodes are reachable exactly once.
Do not require identical arena IDs. Harden the serialized AST reader
to reject unknown operators, invalid encoded lexemes and escapes,
trailing text, malformed nesting, and invalid capacities. Re-lex
token payloads so integer values survive. Bound serializer traversal
and explicitly reject cyclic ASTs. This is NOT source-level code
regeneration or a general AST semantic validator.

## Local acceptance (2026-10-27)

`make -B verify-local`: exit code 0; parser fixture suite 30 valid,
30 invalid, 2 recovery, 8 structural round-trips; 3,000 parser byte
cases and 2,000 malformed serialized AST byte cases; unchanged
5,000 lexer and 5,000 morphology byte cases; original golden fixtures;
GCC ASan/UBSan; ordinary Clang tests; static ELF64 checks.
No hosted GitHub Actions required or requested.

`uname -a` showed Kali WSL2 on
6.18.33.2-microsoft-standard-WSL2. A separate bare-metal Linux kernel,
if installed, was NOT exercised or validated in this receipt.

## Remaining boundaries

No M2 type checker or native Niyah compiler. No original source
position preservation in canonical AST text, no source regeneration,
no guarantee of a semantically valid arbitrary serialized AST;
the CLI `--ast` dump is separate from canonical AST serialization.
User benefit has not been measured.
