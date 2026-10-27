#ifndef NIYAH_EVAL_H
#define NIYAH_EVAL_H

#include "parser.h"

/* Experimental M3 subset: evaluate zero-argument main's constant i64 return.
 * This interprets the AST; it does NOT generate executable machine code. */
typedef struct {
    i64 value;
    Token at;
    const char *error; /* NULL on success; borrowed static string */
} EvalResult;

int eval_constant_main(const Parser *parser, u32 root, EvalResult *out);

#endif
