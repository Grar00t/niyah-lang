#ifndef NIYAH_PARSER_H
#define NIYAH_PARSER_H

#include "lexer.h"

/* M1 syntax tree: 1-based arena indices; zero means absent. Source bytes are
 * borrowed for the lifetime of the arena. No malloc/libc or morphology needed. */
enum { NIYAH_PARSE_MAX_DEPTH = 128 };

typedef enum {
    A_NONE = 0,
    A_PROGRAM, A_FUNC, A_STRUCT, A_PARAM, A_FIELD, A_VAR, A_CONST,
    A_TYPE_NAME, A_TYPE_PTR, A_TYPE_ARRAY,
    A_BLOCK, A_IF, A_WHILE, A_RETURN, A_EXPR_STMT, A_ASSIGN,
    A_BINARY, A_UNARY, A_CALL, A_INDEX, A_MEMBER,
    A_NAME, A_INT, A_STRING
} AstKind;

typedef struct {
    AstKind kind;
    Token tok;              /* original source token; operators retain TokKind */
    u32 first, last, next;   /* ordered children / next sibling; indices, not pointers */
} AstNode;

typedef struct {
    Lexer lex;
    Token look, error_at;
    AstNode *nodes;
    u32 count, capacity, depth;   /* node 0 is reserved */
    u32 error_count;               /* number of diagnostics recorded */
    const char *error;
} Parser;

void parser_init(Parser *p, AstNode *arena, u32 capacity,
                 const u8 *src, u64 source_len);
/* Root (A_PROGRAM) or 0 on error; one diagnostic, no false partial success. */
u32 parser_parse(Parser *p);
const char *ast_name(AstKind kind);

/* Canonical AST shape/payload format, not original source serialization.
 * ast_deserialize borrows the caller-owned payload buffer (and AST arena).
 * Token lexemes, kinds and integer values survive; original line/col do not.
 * On failure return 0; deserialization sets p->error when p is not NULL.
 * Both operations have bounded depth; neither performs M2 semantic analysis. */
u32 ast_serialize(const Parser *p, u32 root, char *out, u32 cap);
u32 ast_deserialize(Parser *p, AstNode *arena, u32 cap,
                    u8 *payload, u32 payload_cap,
                    const char *text, u32 text_len);

#endif
