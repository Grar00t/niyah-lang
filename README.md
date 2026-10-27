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
for parent-child tracing; unavailable tracing fails explicitly.

`make verify-local` also builds and tests the host tools with GCC
ASan/UBSan and ordinary Clang. The generated machine code is not sanitizer
instrumented. GitHub Actions is manual-only; pushing does not start a workflow.

## License

MIT.
