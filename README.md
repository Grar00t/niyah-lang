# niyah-lang

Niyah is an Arabic-keyword language implemented in C11 for Linux x86-64.
It includes a lexer, parser, scalar type checker, interpreter and direct
x86-64 ELF compiler.

## Build and run

Requires a C11 compiler and GNU Make. On Windows, use a Linux x86-64 WSL
distribution.

```sh
make
make test
make verify-local
```

`examples/sum.niy`:

```text
دالة رئيسية() -> صحيح {
    متغير مجموع: صحيح = ٠؛
    متغير س: صحيح = ١؛
    طالما (س <= ٥) {
        مجموع = مجموع + س؛
        س = س + ١؛
    }
    أرجع مجموع؛
}
```

```sh
./niyahparse --check examples/sum.niy
./niyahrun examples/sum.niy          # prints RESULT=15; tool exits 0
./niyahc -o sum examples/sum.niy     # creates a new executable; refuses to overwrite
./sum
echo $?                   # prints 15
```

A generated program returns its main result as the process exit status
modulo 256. Use the interpreter to display the full signed integer result.

## Tools

| Command | Purpose |
| --- | --- |
| `niyahlex FILE` | Print language tokens |
| `niyahlex --annotate FILE` | Add optional morphology candidates for identifiers |
| `niyahlex --morph FILE` | Analyze raw Arabic words |
| `niyahparse FILE` | Validate syntax |
| `niyahparse --ast FILE` | Print the syntax tree |
| `niyahparse --check FILE` | Check scalar types, names, calls and returns |
| `niyahrun FILE` | Interpret a scalar program and print its result |
| `niyahc [-o OUTPUT] [--] FILE` | Compile to Linux x86-64 ELF; default output `a.out` |

## Language support

| Feature | Status |
| --- | --- |
| UTF-8 source, Arabic keywords, Arabic/Latin identifiers | Supported |
| `صحيح`, `بايت`, `منطقي`; strict types without implicit conversions | Supported |
| Globals, local variables, constants and nested scopes | Supported |
| `إذا`, `وإلا`, `طالما`, `أرجع` | Supported |
| Named functions, forward calls and recursion | Supported |
| Checked integer arithmetic and short-circuit boolean operators | Supported |
| Strings, structs, pointers, arrays, indexing and member access | Parsed; rejected by semantic checking |
| Floating point, modules, input/output built-ins, self-hosting | Not implemented |

See the [language reference](docs/language.md),
[architecture](docs/architecture.md), [grammar](docs/grammar.ebnf) and
[lexer/morphology reference](docs/tokenizer.md).

## Verification

`make test` runs the regression suites, compares full signed 64-bit native
results with the interpreter, and separately checks the public 8-bit exit
status. A test-only helper observes the unmodified program's exit-syscall
argument using Linux `ptrace` on its own child. Tests require permission
for parent-child tracing and Linux 5.3 or newer for
[`PTRACE_GET_SYSCALL_INFO`](https://man7.org/linux/man-pages/man2/ptrace.2.html);
unavailable tracing fails explicitly. Test tools also include Python 3.10+
and GNU `objdump`, `readelf` and `timeout`.

The seeded differential suite runs 200 bounded, typed Niyah programs by
default. Its independent i64 oracle defines overflow, truncating division,
remainder and shifts explicitly. Sources and observations are retained under
`TEST_EVIDENCE_DIR` (default `build/test-evidence`); failures also retain a
budget-limited reduced reproducer. To extend the run:

```sh
python3 tests/differential_test.py --seed 20261027 --cases 2000 --evidence-dir build/differential-expanded
```

For measurements against a baseline compiler in its source checkout, run
`python3 tests/benchmark_codegen.py --baseline ../baseline/niyahc --output /tmp/niyah-benchmark --timings`
after other tests stop. The output directory must be new. This separates
full-i64 correctness from untraced timing and records instruction bytes,
stack operations, warmups, paired samples, medians and dispersion.

`make verify-local` also builds and tests the host tools with GCC
ASan/UBSan and ordinary Clang. The generated machine code is not sanitizer
instrumented. GitHub Actions is manual-only; pushing does not start a workflow.

## License

MIT.
