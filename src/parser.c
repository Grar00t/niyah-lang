/* Niyah M1: deterministic recursive-descent parser, arena AST, no libc. */
#include "parser.h"

static const char *const AST_NAMES[] = {
    "NONE", "PROGRAM", "FUNC", "STRUCT", "PARAM", "FIELD", "VAR", "CONST",
    "TYPE_NAME", "TYPE_PTR", "TYPE_ARRAY", "BLOCK", "IF", "WHILE", "RETURN",
    "EXPR_STMT", "ASSIGN", "BINARY", "UNARY", "CALL", "INDEX", "MEMBER",
    "NAME", "INT", "STRING"
};
const char *ast_name(AstKind k) {
    return (u32)k < sizeof AST_NAMES / sizeof AST_NAMES[0] ? AST_NAMES[k] : "?";
}

static u32 fail(Parser *p, const char *reason) {
    p->error_count++;
    if (!p->error) {
        p->error = p->look.kind == T_ERROR ? "lexical error" : reason;
        p->error_at = p->look;
    }
    return 0;
}
static int enter(Parser *p) {
    if (p->depth >= NIYAH_PARSE_MAX_DEPTH) {
        fail(p, "nesting depth exceeded");
        return 0;
    }
    p->depth++;
    return 1;
}
static Token take(Parser *p) {
    Token old = p->look;
    if (p->look.kind != T_EOF) p->look = lex_next(&p->lex);
    return old;
}
static int need(Parser *p, TokKind kind, const char *error) {
    if (p->look.kind != kind) { fail(p, error); return 0; }
    take(p);
    return 1;
}
static u32 node(Parser *p, AstKind kind, Token tok) {
    u32 id;
    if (p->count >= p->capacity) return fail(p, "AST arena capacity exceeded");
    id = p->count++;
    p->nodes[id] = (AstNode){0};
    p->nodes[id].kind = kind;
    p->nodes[id].tok = tok;
    return id;
}
static int child(Parser *p, u32 parent, u32 id) {
    AstNode *n;
    if (!parent || !id) return 0;
    n = &p->nodes[parent];
    if (!n->first) n->first = id;
    else p->nodes[n->last].next = id;
    n->last = id;
    return 1;
}

/* Skip tokens until a statement boundary; caller guarantees look not EOF/RBRACE. */
static void synchronize(Parser *p) {
    while (p->look.kind != T_EOF && p->look.kind != T_RBRACE) {
        if (p->look.kind == T_SEMI) { take(p); return; }
        take(p);
    }
}

void parser_init(Parser *p, AstNode *arena, u32 capacity, const u8 *src, u64 len) {
    *p = (Parser){0};
    p->nodes = arena;
    p->capacity = capacity;
    p->count = 1; /* 0 is an absent child */
    if (!arena || capacity < 2 || !src || len > 0xFFFFFFFFu) {
        p->error = "invalid parser arguments";
        p->error_count = 1;
        return;
    }
    lex_init(&p->lex, src, len);
    p->look = lex_next(&p->lex);
}

static u32 parse_type(Parser *p);
static u32 parse_expr(Parser *p, int minimum);
static u32 parse_block(Parser *p);
static u32 parse_stmt(Parser *p);
static u32 parse_if(Parser *p);

/* type = IDENT | '*' type | '[' INT ']' type */
static u32 parse_type(Parser *p) {
    u32 result = 0, part;
    Token start, size;
    if (!enter(p)) return 0;
    if (p->look.kind == T_IDENT) {
        start = take(p);
        result = node(p, A_TYPE_NAME, start);
    } else if (p->look.kind == T_STAR) {
        start = take(p);
        result = node(p, A_TYPE_PTR, start);
        part = parse_type(p);
        if (!child(p, result, part)) result = 0;
    } else if (p->look.kind == T_LBRACK) {
        take(p);
        if (p->look.kind != T_INT) { fail(p, "expected array size integer"); goto out; }
        size = take(p);
        if (!need(p, T_RBRACK, "expected ']' after array size")) goto out;
        result = node(p, A_TYPE_ARRAY, size);
        part = parse_type(p);
        if (!child(p, result, part)) result = 0;
    } else {
        fail(p, "expected type name, '*' or array type");
    }
out:
    p->depth--;
    return result;
}

static int prec(TokKind op) {
    switch (op) {
    case T_OROR: return 1;
    case T_ANDAND: return 2;
    case T_PIPE: return 3;
    case T_CARET: return 4;
    case T_AMP: return 5;
    case T_EQ: case T_NE: return 6;
    case T_LT: case T_LE: case T_GT: case T_GE: return 7;
    case T_SHL: case T_SHR: return 8;
    case T_PLUS: case T_MINUS: return 9;
    case T_STAR: case T_SLASH: case T_PERCENT: return 10;
    default: return 0;
    }
}
static u32 parse_primary(Parser *p) {
    Token t = p->look;
    u32 result;
    if (t.kind == T_IDENT || t.kind == T_INT || t.kind == T_STRING) {
        take(p);
        return node(p, t.kind == T_IDENT ? A_NAME :
                       t.kind == T_INT ? A_INT : A_STRING, t);
    }
    if (t.kind == T_LPAREN) {
        take(p);
        result = parse_expr(p, 1);
        if (!result || !need(p, T_RPAREN, "expected ')' in expression")) return 0;
        return result;
    }
    return fail(p, "expected expression");
}
static u32 parse_postfix(Parser *p) {
    u32 left = parse_primary(p), expr, wrapper;
    Token t;
    if (!left) return 0;
    while (p->look.kind == T_LPAREN ||
           p->look.kind == T_LBRACK || p->look.kind == T_DOT) {
        t = take(p);
        if (t.kind == T_LPAREN) {
            wrapper = node(p, A_CALL, t);
            if (!child(p, wrapper, left)) return 0;
            if (p->look.kind != T_RPAREN) {
                do {
                    expr = parse_expr(p, 1);
                    if (!child(p, wrapper, expr)) return 0;
                    if (p->look.kind != T_COMMA) break;
                    take(p);
                } while (1);
            }
            if (!need(p, T_RPAREN, "expected ')' after arguments")) return 0;
        } else if (t.kind == T_LBRACK) {
            wrapper = node(p, A_INDEX, t);
            expr = parse_expr(p, 1);
            if (!child(p, wrapper, left) || !child(p, wrapper, expr) ||
                !need(p, T_RBRACK, "expected ']' after index")) return 0;
        } else {
            if (p->look.kind != T_IDENT) return fail(p, "expected field identifier");
            t = take(p);
            wrapper = node(p, A_MEMBER, t);
            if (!child(p, wrapper, left)) return 0;
        }
        if (!wrapper) return 0;
        left = wrapper;
    }
    return left;
}
static u32 parse_unary(Parser *p) {
    u32 result, operand;
    Token op;
    if (!enter(p)) return 0;
    switch (p->look.kind) {
    case T_MINUS: case T_BANG: case T_TILDE: case T_STAR: case T_AMP:
        op = take(p);
        result = node(p, A_UNARY, op);
        operand = parse_unary(p);
        if (!child(p, result, operand)) result = 0;
        break;
    default:
        result = parse_postfix(p);
        break;
    }
    p->depth--;
    return result;
}
/* Binary operators left associative: Pratt-style precedence climbing. */
static u32 parse_expr(Parser *p, int minimum) {
    u32 left, right, wrapper;
    int power;
    Token op;
    if (!enter(p)) return 0;
    left = parse_unary(p);
    while (left && (power = prec(p->look.kind)) >= minimum) {
        op = take(p);
        right = parse_expr(p, power + 1);
        if (!right) { left = 0; break; }
        wrapper = node(p, A_BINARY, op);
        if (!child(p, wrapper, left) || !child(p, wrapper, right)) {
            left = 0; break;
        }
        left = wrapper;
    }
    p->depth--;
    return left;
}

static u32 parse_variable(Parser *p) {
    Token key = take(p), name;
    u32 result, type, initial;
    if (p->look.kind != T_IDENT) return fail(p, "expected variable identifier");
    name = take(p);
    if (!need(p, T_COLON, "expected ':' after variable name")) return 0;
    type = parse_type(p);
    if (!type) return 0;
    result = node(p, key.kind == K_CONST ? A_CONST : A_VAR, name);
    if (!child(p, result, type)) return 0;
    if (p->look.kind == T_ASSIGN) {
        take(p);
        initial = parse_expr(p, 1);
        if (!child(p, result, initial)) return 0;
    } else if (key.kind == K_CONST) {
        return fail(p, "constant declaration requires initializer");
    }
    if (!need(p, T_SEMI, "expected ';' after declaration")) return 0;
    return result;
}
static u32 parse_parameter(Parser *p, AstKind kind) {
    Token name;
    u32 type, result;
    if (p->look.kind != T_IDENT) return fail(p, "expected identifier");
    name = take(p);
    if (!need(p, T_COLON, "expected ':' after identifier")) return 0;
    type = parse_type(p);
    if (!type) return 0;
    result = node(p, kind, name);
    return child(p, result, type) ? result : 0;
}
static u32 parse_function(Parser *p) {
    Token name;
    u32 result, part;
    take(p); /* K_FUNC */
    if (p->look.kind != T_IDENT) return fail(p, "expected function name");
    name = take(p);
    result = node(p, A_FUNC, name);
    if (!result || !need(p, T_LPAREN, "expected '(' after function name")) return 0;
    if (p->look.kind != T_RPAREN) {
        do {
            part = parse_parameter(p, A_PARAM);
            if (!child(p, result, part)) return 0;
            if (p->look.kind != T_COMMA) break;
            take(p);
        } while (1);
    }
    if (!need(p, T_RPAREN, "expected ')' after parameters")) return 0;
    if (p->look.kind == T_ARROW) {
        take(p);
        part = parse_type(p);
        if (!child(p, result, part)) return 0;
    }
    part = parse_block(p);
    return child(p, result, part) ? result : 0;
}
static u32 parse_struct(Parser *p) {
    Token name;
    u32 result, field;
    take(p); /* K_STRUCT */
    if (p->look.kind != T_IDENT) return fail(p, "expected struct name");
    name = take(p);
    result = node(p, A_STRUCT, name);
    if (!result || !need(p, T_LBRACE, "expected '{' after struct name")) return 0;
    while (p->look.kind != T_RBRACE && p->look.kind != T_EOF) {
        field = parse_parameter(p, A_FIELD);
        if (!child(p, result, field) ||
            !need(p, T_SEMI, "expected ';' after struct field")) return 0;
    }
    if (!need(p, T_RBRACE, "expected '}' after struct")) return 0;
    return result;
}
static u32 parse_if(Parser *p) {
    Token key;
    u32 result = 0, part;
    if (!enter(p)) return 0; /* bound arbitrarily long else-if recursion */
    key = take(p);
    if (!need(p, T_LPAREN, "expected '(' after if")) goto out;
    result = node(p, A_IF, key);
    part = parse_expr(p, 1);
    if (!child(p, result, part) ||
        !need(p, T_RPAREN, "expected ')' after if condition")) {
        result = 0; goto out;
    }
    part = parse_block(p);
    if (!child(p, result, part)) { result = 0; goto out; }
    if (p->look.kind == K_ELSE) {
        take(p);
        if (p->look.kind == K_IF) part = parse_if(p);
        else if (p->look.kind == T_LBRACE) part = parse_block(p);
        else { result = fail(p, "expected 'if' or block after else"); goto out; }
        if (!child(p, result, part)) result = 0;
    }
out:
    p->depth--;
    return result;
}
static u32 parse_while(Parser *p) {
    Token key = take(p);
    u32 result, cond, body;
    if (!need(p, T_LPAREN, "expected '(' after while")) return 0;
    result = node(p, A_WHILE, key);
    cond = parse_expr(p, 1);
    if (!child(p, result, cond) ||
        !need(p, T_RPAREN, "expected ')' after while condition")) return 0;
    body = parse_block(p);
    return child(p, result, body) ? result : 0;
}
static u32 parse_return(Parser *p) {
    Token key = take(p);
    u32 result = node(p, A_RETURN, key), part;
    if (p->look.kind != T_SEMI) {
        part = parse_expr(p, 1);
        if (!child(p, result, part)) return 0;
    }
    if (!need(p, T_SEMI, "expected ';' after return")) return 0;
    return result;
}
static u32 parse_stmt(Parser *p) {
    u32 result = 0, left, right;
    Token op;
    if (!enter(p)) return 0;
    switch (p->look.kind) {
    case K_VAR: case K_CONST: result = parse_variable(p); break;
    case K_IF: result = parse_if(p); break;
    case K_WHILE: result = parse_while(p); break;
    case K_RETURN: result = parse_return(p); break;
    case T_LBRACE: result = parse_block(p); break;
    default:
        left = parse_expr(p, 1);
        if (!left) break;
        if (p->look.kind == T_ASSIGN) {
            op = take(p);
            result = node(p, A_ASSIGN, op);
            right = parse_expr(p, 1);
            if (!child(p, result, left) || !child(p, result, right)) {
                result = 0; break;
            }
        } else {
            result = node(p, A_EXPR_STMT, p->look);
            if (!child(p, result, left)) { result = 0; break; }
        }
        if (!need(p, T_SEMI, "expected ';' after expression")) result = 0;
        break;
    }
    p->depth--;
    return result;
}
static u32 parse_block(Parser *p) {
    Token tok = p->look;
    u32 result, statement;
    if (!enter(p)) return 0;
    if (!need(p, T_LBRACE, "expected '{'")) { p->depth--; return 0; }
    result = node(p, A_BLOCK, tok);
    while (result && p->look.kind != T_RBRACE && p->look.kind != T_EOF) {
        statement = parse_stmt(p);
        if (!statement) {
            if (p->look.kind == T_RBRACE || p->look.kind == T_EOF) break;
            synchronize(p);
            continue;
        }
        if (!child(p, result, statement)) { result = 0; break; }
    }
    if (result && !need(p, T_RBRACE, "expected '}' after block")) result = 0;
    p->depth--;
    return result;
}

u32 parser_parse(Parser *p) {
    Token first;
    u32 result, decl;
    if (p->error) return 0;
    first = p->look;
    result = node(p, A_PROGRAM, first);
    if (!result) return 0;
    while (p->look.kind != T_EOF) {
        switch (p->look.kind) {
        case K_FUNC: decl = parse_function(p); break;
        case K_STRUCT: decl = parse_struct(p); break;
        case K_VAR: case K_CONST: decl = parse_variable(p); break;
        default:
            fail(p, "expected top-level declaration");
            decl = 0;
            break;
        }
        if (!decl) {
            if (p->look.kind == T_EOF) break;
            take(p);
            while (p->look.kind != T_EOF &&
                   p->look.kind != K_FUNC && p->look.kind != K_STRUCT &&
                   p->look.kind != K_VAR && p->look.kind != K_CONST) {
                take(p);
            }
            continue;
        }
        if (!child(p, result, decl)) return 0;
    }
    return p->error ? 0 : result;
}

/* ---- AST canonical serialization (dump/reparse round-trip) ---- */

static u32 rt_len(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static int rt_eq(const char *a, const char *b, u32 n) {
    for (u32 i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}
static int kind_has_payload(AstKind k) {
    switch (k) {
    case A_FUNC: case A_STRUCT: case A_PARAM: case A_FIELD:
    case A_VAR: case A_CONST: case A_NAME: case A_MEMBER:
    case A_TYPE_NAME: case A_TYPE_ARRAY: case A_INT: case A_STRING:
    case A_BINARY: case A_UNARY: case A_ASSIGN:
        return 1;
    default: return 0;
    }
}
static int kind_payload_is_tokkind(AstKind k) {
    return k == A_BINARY || k == A_UNARY || k == A_ASSIGN;
}
static TokKind tok_kind_from_name(const char *name, u32 len) {
    for (int k = 0; k < (int)T_COUNT; k++) {
        const char *n = tok_name((TokKind)k);
        if (rt_len(n) == len && rt_eq(n, name, len)) return (TokKind)k;
    }
    return T_EOF;
}

typedef struct { char *out; u32 cap, len; int failed; } EmitCtx;
static void em(EmitCtx *c, const char *s, u32 n) {
    if (c->failed) return;
    if (n > c->cap - c->len) { c->failed = 1; return; }
    for (u32 i = 0; i < n; i++) c->out[c->len++] = s[i];
}
static void ems(EmitCtx *c, const char *s) { em(c, s, rt_len(s)); }
static void emq(EmitCtx *c, const u8 *s, u32 n) {
    em(c, "\"", 1);
    for (u32 i = 0; i < n; i++) {
        if (s[i] == '"' || s[i] == '\\') em(c, "\\", 1);
        em(c, (const char *)&s[i], 1);
    }
    em(c, "\"", 1);
}
static void em_node(EmitCtx *c, const Parser *p, u32 id, u32 depth) {
    const AstNode *n;
    u32 child, siblings = 0;
    if (c->failed) return;
    if (!id || id >= p->count || depth >= NIYAH_PARSE_MAX_DEPTH) {
        c->failed = 1; return;
    }
    n = &p->nodes[id];
    if (n->kind <= A_NONE || n->kind > A_STRING) { c->failed = 1; return; }
    em(c, "(", 1);
    ems(c, ast_name(n->kind));
    if (kind_has_payload(n->kind)) {
        em(c, " ", 1);
        if (kind_payload_is_tokkind(n->kind)) {
            const char *tn = tok_name(n->tok.kind);
            emq(c, (const u8 *)tn, rt_len(tn));
        } else if (n->tok.start && n->tok.len) {
            emq(c, n->tok.start, n->tok.len);
        } else {
            em(c, "\"\"", 2);
        }
    }
    child = n->first;
    while (child && !c->failed) {
        if (child >= p->count || ++siblings >= p->count) {
            c->failed = 1; return;
        }
        em(c, " ", 1);
        em_node(c, p, child, depth + 1);
        child = p->nodes[child].next;
    }
    em(c, ")", 1);
}
u32 ast_serialize(const Parser *p, u32 root, char *out, u32 cap) {
    EmitCtx c = { out, cap, 0, 0 };
    if (!p || !p->nodes || !out || !cap ||
        p->count > p->capacity || !root || root >= p->count) return 0;
    em_node(&c, p, root, 0);
    if (c.failed) return 0;
    return c.len;
}

typedef struct {
    const char *text; u32 pos, len;
    u8 *pay; u32 pay_cap, pay_used;
} DesCtx;

static void dsws(DesCtx *c) {
    while (c->pos < c->len) {
        char ch = c->text[c->pos];
        if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') c->pos++;
        else return;
    }
}
static int dskind(DesCtx *c, AstKind *out) {
    u32 start = c->pos;
    while (c->pos < c->len && c->text[c->pos] != ' ' && c->text[c->pos] != '(' &&
           c->text[c->pos] != ')' && c->text[c->pos] != '\n' &&
           c->text[c->pos] != '\r' && c->text[c->pos] != '\t') c->pos++;
    u32 n = c->pos - start;
    for (int k = 1; k <= (int)A_STRING; k++) {
        const char *name = ast_name((AstKind)k);
        if (rt_len(name) == n && rt_eq(name, c->text + start, n)) {
            *out = (AstKind)k; return 1;
        }
    }
    return 0;
}
static int dsq(DesCtx *c, u32 *so, u32 *sl) {
    if (c->pos >= c->len || c->text[c->pos] != '"') return 0;
    c->pos++;
    u32 start = c->pay_used;
    while (c->pos < c->len) {
        char ch = c->text[c->pos++];
        if (ch == '"') { *so = start; *sl = c->pay_used - start; return 1; }
        if (ch == '\\') {
            if (c->pos >= c->len) return 0;
            ch = c->text[c->pos++];
            if (ch != '\\' && ch != '"') return 0;
        }
        if (c->pay_used >= c->pay_cap) return 0;
        c->pay[c->pay_used++] = (u8)ch;
    }
    return 0;
}
static u32 ds_node(DesCtx *c, Parser *p, u32 depth) {
    AstKind kind;
    u32 ts = 0, tl = 0, id, prev = 0, child;
    if (depth >= NIYAH_PARSE_MAX_DEPTH) return 0;
    dsws(c);
    if (c->pos >= c->len || c->text[c->pos] != '(') return 0;
    c->pos++;
    dsws(c);
    if (!dskind(c, &kind)) return 0;
    if (kind_has_payload(kind)) {
        dsws(c);
        if (!dsq(c, &ts, &tl)) return 0;
    }
    if (p->count >= p->capacity) return 0;
    id = p->count++;
    p->nodes[id] = (AstNode){0};
    p->nodes[id].kind = kind;
    if (kind_has_payload(kind)) {
        if (kind_payload_is_tokkind(kind)) {
            TokKind op = tok_kind_from_name((const char *)(c->pay + ts), tl);
            if ((kind == A_BINARY && !prec(op)) ||
                (kind == A_ASSIGN && op != T_ASSIGN) ||
                (kind == A_UNARY && op != T_MINUS && op != T_BANG &&
                 op != T_TILDE && op != T_STAR && op != T_AMP)) return 0;
            p->nodes[id].tok.kind = op;
        } else {
            /* Recover numeric value and token kind, not just the source bytes.
             * Invalid UTF-8, bad escapes and noncanonical payloads fail closed. */
            Lexer lexer;
            Token tok;
            TokKind want = kind == A_INT || kind == A_TYPE_ARRAY ? T_INT :
                           kind == A_STRING ? T_STRING : T_IDENT;
            if (!tl) return 0;
            lex_init(&lexer, c->pay + ts, tl);
            tok = lex_next(&lexer);
            if (tok.kind != want || tok.start != c->pay + ts ||
                tok.len != tl || lex_next(&lexer).kind != T_EOF) return 0;
            p->nodes[id].tok = tok;
        }
    }
    while (1) {
        dsws(c);
        if (c->pos >= c->len) return 0;
        if (c->text[c->pos] == ')') { c->pos++; break; }
        child = ds_node(c, p, depth + 1);
        if (!child) return 0;
        if (!prev) p->nodes[id].first = child;
        else p->nodes[prev].next = child;
        p->nodes[id].last = child;
        prev = child;
    }
    return id;
}
u32 ast_deserialize(Parser *p, AstNode *arena, u32 cap,
                    u8 *payload, u32 payload_cap,
                    const char *text, u32 text_len) {
    DesCtx c = { text, 0, text_len, payload, payload_cap, 0 };
    u32 root;
    if (!p) return 0;
    *p = (Parser){0};
    if (!arena || cap < 2 || (!payload && payload_cap) ||
        !text || !text_len) {
        p->error = "invalid deserialize arguments";
        return 0;
    }
    p->nodes = arena;
    p->capacity = cap;
    p->count = 1;
    root = ds_node(&c, p, 0);
    if (!root) { p->error = "deserialize failed"; return 0; }
    dsws(&c);
    if (c.pos != c.len) { p->error = "trailing data"; return 0; }
    return root;
}
