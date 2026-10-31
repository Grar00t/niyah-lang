#ifndef NIYAH_PARSER_H
#define NIYAH_PARSER_H
#include <stdint.h>
#include "lexer.h"
enum { NIYAH_PARSE_MAX_DEPTH = 128 };
typedef enum {
    A_NONE = 0, A_PROGRAM, A_FUNC, A_STRUCT, A_PARAM, A_FIELD, A_VAR, A_CONST,
    A_TYPE_NAME, A_TYPE_PTR, A_TYPE_ARRAY,
    A_BLOCK, A_IF, A_WHILE, A_RETURN, A_EXPR_STMT, A_ASSIGN,
    A_BINARY, A_UNARY, A_CALL, A_INDEX, A_MEMBER,
    A_NAME, A_INT, A_STRING, A_TENSOR_OP
} AstKind;
typedef enum {
    TI_NONE, TI_INPUT_A, TI_INPUT_B, TI_GEMM, TI_EMIT, TI_RESERVE,
    TI_ADD, TI_TRANSPOSE, TI_RELU
} TensorIntrinsic;
#define NIYAH_TENSOR_NO_VALUE UINT32_MAX
typedef struct {
    u32 opcode;
    u32 metadata;
    u32 syntax;
    u32 left_idx;
    u32 right_idx;
    /* Lowered value identity; left_idx/right_idx retain source AST indices. */
    u32 value_id, value_type;
    u32 operand_ids[2];
    int tmm;
    int lifespan_start;
    int lifespan_end;
} Node_TensorOp;
typedef struct {
    AstKind kind;
    Token tok;
    u32 first, last, next;
    Node_TensorOp tensor;
} AstNode;
typedef struct {
    Lexer lex;
    Token look, error_at;
    AstNode *nodes;
    u32 count, capacity, depth;
    u32 error_count;
    const char *error;
    const u8 *source;
    u64 source_len;
    u32 has_tensor;
} Parser;
void parser_init(Parser *p, AstNode *arena, u32 capacity, const u8 *src, u64 source_len);
u32 parser_parse(Parser *p);
int parser_tensor_reserved(Token name);
const char *ast_name(AstKind kind);
u32 ast_serialize(const Parser *p, u32 root, char *out, u32 cap);
u32 ast_deserialize(Parser *p, AstNode *arena, u32 cap, u8 *payload, u32 payload_cap, const char *text, u32 text_len);
#endif
