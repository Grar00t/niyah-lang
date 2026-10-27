# Experimental AST constant execution — 2026-10-27

## What works

\`make niyahrun\` builds a hosted Linux C11 interpreter using the existing
M1 AST. Example:

    printf 'دالة رئيسية() -> صحيح { أرجع ٢ + ٣؛ }\n' > /tmp/five.نيه
    ./niyahrun /tmp/five.نيه

Expected stdout: \`RESULT=5\`; exit code 0.

Supported: one top-level zero-argument \`رئيسية() -> صحيح\` function with
one return statement containing an integer literal arithmetic expression.
Supported operators are unary minus, bitwise complement, +, -, *, / and %.
Arithmetic uses checked signed 64-bit calculations. Division by zero and
overflow fail with nonzero exit; unsupported nodes fail closed.
This interpretation does **not** execute control flow, variables, function
calls or types and does not generate a machine-code executable.

\`niyahrun\` uses hosted C11 file and output facilities. This is a deliberate
experimental interpreter layer, separate from freestanding \`niyahlex\` and
\`niyahparse\`. Neither compiler M2 completeness nor native ELF code
generation nor self-hosting has been achieved.

## Verification

\`make test\` runs two successful arithmetic fixtures plus explicit
division-by-zero, overflow, and unsupported-source failure cases.
\`make verify-local\` additionally checks GCC ASan/UBSan and ordinary Clang
execution of the arithmetic fixture. Do not run GitHub-hosted Actions;
the repository workflow remains manual-only.

Source: \`src/eval.c\`, \`src/eval.h\`, \`src/run_cli.c\`.
Tests: \`tests/run_cli_test.sh\`.
