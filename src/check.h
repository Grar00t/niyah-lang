#ifndef NIYAH_CHECK_H
#define NIYAH_CHECK_H
#include "parser.h"
enum { CHECK_MAX_SYMBOLS = 2048, CHECK_MAX_DEPTH = 128 };
typedef enum { TY_ERROR=0, TY_I64, TY_BYTE, TY_BOOL, TY_VOID } CheckType;
typedef struct { Token name; CheckType type; u32 scope; u8 is_const; } CheckSymbol;
typedef struct {
    const Parser *parser;
    CheckSymbol symbols[CHECK_MAX_SYMBOLS];
    u32 count, scope, errors, checked_nodes;
    const char *message;
    Token at;
} Checker;
void checker_init(Checker *c, const Parser *p);
int checker_run(Checker *c, u32 root);
const char *checker_type_name(CheckType t);
#endif
