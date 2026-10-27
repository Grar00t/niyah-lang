#include "eval.h"
#include "check.h"
#include <stdlib.h>
#include <limits.h>

typedef struct {
    Token name;
    i64 value;
    u8 initialized;
    u8 immutable;
} Binding;
typedef struct {
    const Parser *parser;
    EvalResult *result;
    u32 root, count, globals, frame, calls;
    Binding bindings[EVAL_MAX_BINDINGS];
} Runtime;

static int same(Token a, Token b) {
    u32 i;
    if (a.len != b.len || !a.start || !b.start) return 0;
    for (i = 0; i < a.len; ++i) if (a.start[i] != b.start[i]) return 0;
    return 1;
}
static int named(Token t, const char *s) {
    u32 i = 0;
    while (s[i]) {
        if (i >= t.len || !t.start || t.start[i] != (u8)s[i]) return 0;
        ++i;
    }
    return i == t.len;
}
static const AstNode *node(Runtime *r, u32 id) {
    return &r->parser->nodes[id];
}
static int fail(Runtime *r, u32 id, const char *message) {
    if (!r->result->error) {
        r->result->error = message;
        if (id && id < r->parser->count) r->result->at = node(r, id)->tok;
    }
    return 0;
}
static int tick(Runtime *r, u32 id) {
    if (!id || id >= r->parser->count) return fail(r, id, "invalid AST");
    if (r->result->steps >= EVAL_MAX_STEPS)
        return fail(r, id, "execution step limit exceeded");
    ++r->result->steps;
    return 1;
}
static Binding *lookup(Runtime *r, Token name) {
    u32 i = r->count;
    while (i > r->frame) {
        --i;
        if (same(r->bindings[i].name, name)) return &r->bindings[i];
    }
    i = r->globals;
    while (i) {
        --i;
        if (same(r->bindings[i].name, name)) return &r->bindings[i];
    }
    return NULL;
}
static int bind(Runtime *r, u32 id, i64 value, int initialized, int immutable) {
    Binding *b;
    if (r->count == EVAL_MAX_BINDINGS)
        return fail(r, id, "runtime binding capacity exceeded");
    b = &r->bindings[r->count++];
    *b = (Binding){node(r, id)->tok, value, (u8)initialized, (u8)immutable};
    return 1;
}
static u32 function(Runtime *r, Token name) {
    u32 id = node(r, r->root)->first;
    while (id) {
        if (node(r, id)->kind == A_FUNC && same(node(r, id)->tok, name)) return id;
        id = node(r, id)->next;
    }
    return 0;
}
static int expression(Runtime *r, u32 id, u32 depth, i64 *value);
/* statement/block: -1 error, 0 fallthrough, 1 return. */
static int statement(Runtime *r, u32 id, u32 depth, i64 *value);
static int block(Runtime *r, u32 id, u32 depth, i64 *value) {
    u32 mark = r->count, item;
    int flow = 0;
    if (depth > CHECK_MAX_DEPTH) {
        fail(r, id, "statement nesting exceeded");
        return -1;
    }
    item = node(r, id)->first;
    while (item) {
        flow = statement(r, item, depth + 1, value);
        if (flow) break;
        item = node(r, item)->next;
    }
    r->count = mark;
    return flow;
}
static int invoke(Runtime *r, u32 fn, const i64 *args, u32 argc, i64 *value) {
    u32 mark = r->count, old_frame = r->frame, part, index = 0;
    int has_result = 0, flow;
    if (r->calls >= EVAL_MAX_CALLS) return fail(r, fn, "call depth exceeded");
    ++r->calls;
    r->frame = mark;
    part = node(r, fn)->first;
    while (part && node(r, part)->kind == A_PARAM) {
        if (index >= argc || !bind(r, part, args[index], 1, 0)) goto bad;
        ++index;
        part = node(r, part)->next;
    }
    if (index != argc) {
        fail(r, fn, "argument count mismatch");
        goto bad;
    }
    if (part && node(r, part)->kind != A_BLOCK) {
        has_result = 1;
        part = node(r, part)->next;
    }
    if (!part || node(r, part)->kind != A_BLOCK) {
        fail(r, fn, "function body missing");
        goto bad;
    }
    *value = 0;
    flow = block(r, part, 0, value);
    if (flow < 0) goto bad;
    if (!flow && has_result) {
        fail(r, fn, "function ended without return");
        goto bad;
    }
    r->count = mark;
    r->frame = old_frame;
    --r->calls;
    return 1;
bad:
    r->count = mark;
    r->frame = old_frame;
    --r->calls;
    return 0;
}
/* Keep argument storage out of every unary/binary expression frame. Deep
 * expressions inside recursive calls otherwise multiply the stack cost. */
static __attribute__((noinline)) int call_expression(Runtime *r, u32 id,
                                                      u32 depth, i64 *value) {
    u32 callee = node(r, id)->first, arg, fn, argc = 0;
    i64 args[EVAL_MAX_ARGS];
    if (!callee || node(r, callee)->kind != A_NAME)
        return fail(r, id, "call target must be a function name");
    if (lookup(r, node(r, callee)->tok))
        return fail(r, callee, "call target is not a function");
    fn = function(r, node(r, callee)->tok);
    if (!fn) return fail(r, callee, "undefined identifier");
    arg = node(r, callee)->next;
    while (arg) {
        if (argc == EVAL_MAX_ARGS) return fail(r, id, "argument capacity exceeded");
        if (!expression(r, arg, depth + 1, &args[argc++])) return 0;
        arg = node(r, arg)->next;
    }
    return invoke(r, fn, args, argc, value);
}
static int expression(Runtime *r, u32 id, u32 depth, i64 *value) {
    const AstNode *n;
    Binding *binding;
    u32 left, right;
    i64 a, b, result;
    if (!id || id >= r->parser->count) return fail(r, id, "invalid AST");
    if (depth > CHECK_MAX_DEPTH) return fail(r, id, "expression nesting exceeded");
    n = node(r, id);
    switch (n->kind) {
    case A_INT:
        *value = n->tok.value;
        return 1;
    case A_NAME:
        binding = lookup(r, n->tok);
        if (!binding) return fail(r, id, "undefined identifier");
        if (!binding->initialized) return fail(r, id, "uninitialized variable");
        *value = binding->value;
        return 1;
    case A_CALL:
        return call_expression(r, id, depth, value);
    case A_UNARY:
        if (!expression(r, n->first, depth + 1, &a)) return 0;
        switch (n->tok.kind) {
        case T_MINUS:
            if (__builtin_sub_overflow((i64)0, a, &result))
                return fail(r, id, "integer overflow");
            *value = result; return 1;
        case T_TILDE: *value = ~a; return 1;
        case T_BANG: *value = !a; return 1;
        default: return fail(r, id, "unsupported unary operator");
        }
    case A_BINARY:
        left = n->first;
        right = left ? node(r, left)->next : 0;
        if (!left || !right) return fail(r, id, "invalid binary AST");
        if (!expression(r, left, depth + 1, &a)) return 0;
        if (n->tok.kind == T_ANDAND && !a) { *value = 0; return 1; }
        if (n->tok.kind == T_OROR && a) { *value = 1; return 1; }
        if (!expression(r, right, depth + 1, &b)) return 0;
        switch (n->tok.kind) {
        case T_PLUS:
            if (__builtin_add_overflow(a, b, &result)) return fail(r, id, "integer overflow");
            break;
        case T_MINUS:
            if (__builtin_sub_overflow(a, b, &result)) return fail(r, id, "integer overflow");
            break;
        case T_STAR:
            if (__builtin_mul_overflow(a, b, &result)) return fail(r, id, "integer overflow");
            break;
        case T_SLASH:
        case T_PERCENT:
            if (!b) return fail(r, id, n->tok.kind == T_SLASH ? "division by zero" : "modulo by zero");
            if (a == LONG_MIN && b == -1) return fail(r, id, "integer overflow");
            result = n->tok.kind == T_SLASH ? a / b : a % b;
            break;
        case T_SHL:
            if (b < 0 || b > 63) return fail(r, id, "invalid shift count");
            result = a;
            while (b--) {
                if (__builtin_add_overflow(result, result, &result))
                    return fail(r, id, "integer overflow");
            }
            break;
        case T_SHR:
            if (b < 0 || b > 63) return fail(r, id, "invalid shift count");
            result = a >> b;
            break;
        case T_AMP: result = a & b; break;
        case T_PIPE: result = a | b; break;
        case T_CARET: result = a ^ b; break;
        case T_EQ: result = a == b; break;
        case T_NE: result = a != b; break;
        case T_LT: result = a < b; break;
        case T_LE: result = a <= b; break;
        case T_GT: result = a > b; break;
        case T_GE: result = a >= b; break;
        case T_ANDAND: result = a && b; break;
        case T_OROR: result = a || b; break;
        default: return fail(r, id, "unsupported binary operator");
        }
        *value = result;
        return 1;
    default:
        return fail(r, id, "unsupported expression");
    }
}
static int statement(Runtime *r, u32 id, u32 depth, i64 *value) {
    const AstNode *n;
    u32 a, b, other;
    i64 result = 0;
    Binding *binding;
    int flow;
    if (!tick(r, id)) return -1;
    if (depth > CHECK_MAX_DEPTH) {
        fail(r, id, "statement nesting exceeded");
        return -1;
    }
    n = node(r, id);
    switch (n->kind) {
    case A_VAR:
    case A_CONST:
        a = n->first;
        b = a ? node(r, a)->next : 0;
        if (b && !expression(r, b, 0, &result)) return -1;
        return bind(r, id, result, b != 0, n->kind == A_CONST) ? 0 : -1;
    case A_ASSIGN:
        a = n->first;
        b = a ? node(r, a)->next : 0;
        if (!a || !b || node(r, a)->kind != A_NAME) {
            fail(r, id, "invalid assignment target");
            return -1;
        }
        binding = lookup(r, node(r, a)->tok);
        if (!binding) { fail(r, a, "undefined identifier"); return -1; }
        if (binding->immutable) { fail(r, a, "cannot assign to constant"); return -1; }
        if (!expression(r, b, 0, &result)) return -1;
        binding->value = result;
        binding->initialized = 1;
        return 0;
    case A_EXPR_STMT:
        return expression(r, n->first, 0, &result) ? 0 : -1;
    case A_RETURN:
        *value = 0;
        if (n->first && !expression(r, n->first, 0, value)) return -1;
        return 1;
    case A_BLOCK:
        return block(r, id, depth, value);
    case A_IF:
        a = n->first;
        b = a ? node(r, a)->next : 0;
        other = b ? node(r, b)->next : 0;
        if (!expression(r, a, 0, &result)) return -1;
        if (result) return block(r, b, depth + 1, value);
        if (other) {
            if (node(r, other)->kind == A_IF)
                return statement(r, other, depth + 1, value);
            return block(r, other, depth + 1, value);
        }
        return 0;
    case A_WHILE:
        a = n->first;
        b = a ? node(r, a)->next : 0;
        for (;;) {
            if (!tick(r, id)) return -1;
            if (!expression(r, a, 0, &result)) return -1;
            if (!result) return 0;
            flow = block(r, b, depth + 1, value);
            if (flow) return flow;
        }
    default:
        fail(r, id, "unsupported statement");
        return -1;
    }
}

int eval_main(const Parser *p, u32 root, EvalResult *out) {
    Checker checker;
    Runtime *r;
    u32 id, main_id = 0, part, index;
    i64 value = 0;
    int ok = 0;
    if (!out) return 0;
    *out = (EvalResult){0};
    if (!p || !root || root >= p->count || p->nodes[root].kind != A_PROGRAM) {
        out->error = "invalid program";
        return 0;
    }
    checker_init(&checker, p);
    if (!checker_run(&checker, root)) {
        out->error = checker.message ? checker.message : "semantic error";
        out->at = checker.at;
        return 0;
    }
    r = calloc(1, sizeof(*r));
    if (!r) { out->error = "out of memory"; return 0; }
    r->parser = p;
    r->result = out;
    r->root = root;
    for (id = p->nodes[root].first; id; id = p->nodes[id].next) {
        if (p->nodes[id].kind == A_FUNC && named(p->nodes[id].tok, "رئيسية")) main_id = id;
        if (p->nodes[id].kind == A_VAR || p->nodes[id].kind == A_CONST) {
            if (!bind(r, id, 0, 0, p->nodes[id].kind == A_CONST)) goto done;
        }
    }
    r->globals = r->count;
    r->frame = r->globals;
    if (!main_id) { fail(r, root, "expected function رئيسية"); goto done; }
    part = p->nodes[main_id].first;
    if (!part || p->nodes[part].kind != A_TYPE_NAME || !named(p->nodes[part].tok, "صحيح")) {
        fail(r, main_id, "main must return صحيح and have no parameters");
        goto done;
    }
    index = 0;
    for (id = p->nodes[root].first; id; id = p->nodes[id].next) {
        if (p->nodes[id].kind == A_VAR || p->nodes[id].kind == A_CONST) {
            part = p->nodes[id].first;
            part = part ? p->nodes[part].next : 0;
            value = 0;
            if (!tick(r, id)) goto done;
            if (part && !expression(r, part, 0, &value)) goto done;
            r->bindings[index].value = value;
            r->bindings[index++].initialized = 1;
        }
    }
    if (!invoke(r, main_id, NULL, 0, &value)) goto done;
    out->value = value;
    ok = 1;
done:
    free(r);
    return ok;
}
int eval_constant_main(const Parser *p, u32 root, EvalResult *out) {
    return eval_main(p, root, out);
}
