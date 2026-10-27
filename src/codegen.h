#ifndef NIYAH_CODEGEN_H
#define NIYAH_CODEGEN_H

#include "parser.h"

/* Native Linux x86-64 ET_EXEC emitter. The compiler is hosted C; generated
 * files use only Linux write/exit syscalls, with separate RX/RW segments and
 * a non-executable stack. No source text, interpreter, assembler or linker
 * is embedded or invoked. Inputs must be an arena produced by parser_parse.
 * The semantic checker runs before emission. The entry is رئيسية() -> صحيح.
 *
 * Compile bounds: 65,536 AST nodes, 1,024 functions, 2,048 storage slots per
 * function and globals, 64 arguments per call, 16,384 relocations, nesting
 * depth 128, and a caller-supplied output capacity (CLI: 1 MiB).
 * Execution bounds: 128 active calls and 1,000,000 statement/loop-test ticks.
 * Locals without initializers are unreadable until assigned. Globals are
 * initialized in declaration order; a no-initializer global becomes zero
 * at its declaration, while future globals remain unreadable.
 * Arguments evaluate left to right. && and || short circuit. Signed i64
 * arithmetic and left shifts check overflow; >> is arithmetic, counts 0..63.
 * Runtime failures write stderr and exit 120 (overflow), 121 (division by
 * zero), 122 (shift count), 123 (fuel), 124 (call depth), 125 (uninitialized).
 * Normal execution exits with the low 8 bits of main's return value.
 */
enum {
    CODEGEN_MAX_FUNCTIONS = 1024,
    CODEGEN_MAX_SLOTS = 2048,
    CODEGEN_MAX_ARGS = 64,
    CODEGEN_MAX_RELOCS = 16384,
    CODEGEN_MAX_NODES = 65536,
    CODEGEN_CALL_LIMIT = 128,
    CODEGEN_FUEL = 1000000
};
typedef struct {
    u32 size;                  /* complete ELF byte count, only on success */
    Token at;
    const char *error;          /* static string, NULL on success */
    int semantic_error;         /* checker diagnostic, not an emitter error */
} CodegenResult;

int codegen_elf64(const Parser *parser, u32 root, u8 *output, u32 capacity,
                  CodegenResult *result);
#endif
