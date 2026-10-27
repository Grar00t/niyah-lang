# Language reference

Niyah source is UTF-8. Arabic keywords can be mixed with Arabic or Latin
identifiers. Identifier identity is byte-exact; visually similar Unicode
spellings are distinct. See [grammar.ebnf](grammar.ebnf) for full parser
syntax and [tokenizer.md](tokenizer.md) for lexical details.

## Scalar types

| Type | Meaning | Operations |
| --- | --- | --- |
| `صحيح` | Signed 64-bit integer | Arithmetic, bitwise operations, shifts, comparisons |
| `بايت` | Unsigned byte | Assignment, parameter/return passing, equality |
| `منطقي` | Boolean | `!`, `&&`, `||`, equality, conditions |

Integer literals have type `صحيح`. Comparisons produce `منطقي`. There are
no implicit conversions between these types. Omitting a function's return
type declares a void function.

## Declarations and scope

```text
ثابت حد: صحيح = ٥؛
متغير مجموع: صحيح = ٠؛
```

`ثابت` requires an initializer and cannot be assigned afterward. `متغير`
can be assigned with `=`. Duplicate names in the same scope are errors;
nested blocks may shadow outer names. Functions and globals share one
namespace. Local variables and parameters may shadow a function name;
calling such a scalar binding is an error.

Global variable initializers can refer to earlier globals. Function
signatures are visible throughout the program and function bodies can
refer to all globals.

## Functions

```text
دالة زد(س: صحيح) -> صحيح {
    أرجع س + ١؛
}

دالة رئيسية() -> صحيح {
    أرجع زد(٤)؛
}
```

Arguments must match the declared parameter count and types exactly.
Forward references, recursion and mutual recursion are accepted.
A non-void function must return a value on every provable path. Loops alone
do not establish that guarantee. A void function returns with `أرجع؛`;
its calls may be statements but cannot supply values.

Function values, indirect calls and calling the result of a call are
unsupported. The executable entry point is `رئيسية() -> صحيح` without
parameters.

## Control flow

```text
دالة رئيسية() -> صحيح {
    متغير س: صحيح = ٠؛
    طالما (س < ٥) {
        س = س + ١؛
    }
    إذا (س == ٥) {
        أرجع س؛
    } وإلا {
        أرجع ٠؛
    }
}
```

`إذا` and `طالما` require boolean conditions. `وإلا إذا` chains conditions.
Blocks introduce scopes. An expression followed by `؛` discards its value.
ASCII `;` and `,` may replace Arabic `؛` and `،`.

## Supported syntax boundary

The parser also recognizes strings, structs, pointers, arrays, indexing
and member access. These forms are rejected by semantic checking and
execution. Type conversions, floating point, modules and input/output
built-ins are not implemented.

## Execution rules

Run with `niyahrun FILE`, or compile with `niyahc [-o OUTPUT] [--] FILE`.
The compiler defaults to `a.out` and refuses to overwrite an existing path.
The interpreter prints `RESULT=<signed integer>` and exits zero on success.
The generated executable exits with the returned integer modulo 256.

- Globals initialize in source order. A global without an initializer is
  zero after its declaration. Reading a global before initialization or
  assignment fails, including through a function called by an initializer.
  An assignment may initialize a later global early; its declaration still
  runs in source order and replaces that value.
- A local declared without an initializer must be assigned before a read.
- Arithmetic overflow, division by zero and invalid shift counts fail.
  Shift counts are 0–63; right shift is arithmetic and left shift checks
  signed overflow.
- `&&` and `||` evaluate the right operand only when needed.
- Execution is bounded to 1,000,000 steps, counted for statements, loop
  condition checks and global initializations; 128 nested calls; and at
  most 64 arguments per call.

| Tool | Exit codes |
| --- | --- |
| `niyahparse` | 0 valid; 1 usage/read; 2 syntax; 3 output; 4 semantic |
| `niyahrun` | 0 success; 1 usage/read; 2 syntax; 3 runtime; 4 output; 5 semantic |
| `niyahc` | 0 compiled; 1 usage/read/output; 2 syntax; 3 semantic; 4 code generation |
| Generated executable | Main result modulo 256; runtime failures below |

Generated runtime failures print a diagnostic and exit with: 120 overflow,
121 division by zero, 122 invalid shift, 123 exhausted statement fuel,
124 call depth, or 125 uninitialized read. A normal program can return the
same numeric exit status; inspect the diagnostic to distinguish them.
