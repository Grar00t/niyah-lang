#ifndef NIYAH_EVAL_H
#define NIYAH_EVAL_H
#include "check.h"

/* Reference interpreter for the checked scalar language. */
enum {
    EVAL_MAX_STEPS = 1000000,
    EVAL_MAX_CALLS = 128,
    EVAL_MAX_ARGS = 64,
    EVAL_MAX_BINDINGS = CHECK_MAX_SYMBOLS * (EVAL_MAX_CALLS + 1)
};
typedef struct {
    i64 value;
    Token at;
    const char *error;
    u32 steps;
} EvalResult;

/* Source and AST storage must remain alive. No output or external effects. */
int eval_main(const Parser *parser, u32 root, EvalResult *out);
/* Compatibility entry point; now accepts the same programs as eval_main. */
int eval_constant_main(const Parser *parser, u32 root, EvalResult *out);
#endif
