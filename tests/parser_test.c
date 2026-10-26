#include "../src/parser.h"
#include <stdio.h>
#include <string.h>

enum { CAP = 16384 };
static AstNode arena[CAP];
static u8 seen[CAP];
static u32 stack[CAP];
static int failed;

static int graph_ok(const Parser *p, u32 root) {
    u32 sp = 0, visited = 0, id, child;
    if (root == 0 || root >= p->count || p->count > CAP) return 0;
    memset(seen, 0, sizeof seen);
    stack[sp++] = root;
    while (sp) {
        id = stack[--sp];
        if (!id || id >= p->count || seen[id] || ++visited >= CAP) return 0;
        seen[id] = 1;
        if (!p->nodes[id].kind || p->nodes[id].kind > A_STRING) return 0;
        child = p->nodes[id].first;
        if ((child == 0) != (p->nodes[id].last == 0)) return 0;
        while (child) {
            if (child >= p->count || sp >= CAP) return 0;
            stack[sp++] = child;
            child = p->nodes[child].next;
        }
    }
    return visited == p->count - 1;
}
static u32 parse(const char *s, Parser *p) {
    parser_init(p, arena, CAP, (const u8 *)s, (u64)strlen(s));
    return parser_parse(p);
}
static void ok(const char *name, const char *s) {
    Parser p;
    u32 root = parse(s, &p);
    if (!root || !graph_ok(&p, root)) {
        fprintf(stderr, "FAIL valid: %s, %s at %u:%u (%s)\n", name,
                p.error ? p.error : "invalid AST graph",
                p.error_at.line, p.error_at.col, tok_name(p.error_at.kind));
        failed++;
    }
}
static void bad(const char *name, const char *s) {
    Parser p;
    u32 root = parse(s, &p);
    if (root || !p.error) {
        fprintf(stderr, "FAIL invalid accepted: %s\n", name);
        failed++;
    }
}
static void bad_multi(const char *name, const char *s, u32 want_min) {
    Parser p;
    u32 root = parse(s, &p);
    if (root || !p.error) {
        fprintf(stderr, "FAIL not rejected: %s\n", name); failed++; return;
    }
    if (p.error_count < want_min) {
        fprintf(stderr, "FAIL only %u diagnostics, want >=%u: %s\n",
                p.error_count, want_min, name);
        failed++;
    }
}

static int tr_has_payload(AstKind k) {
    switch (k) {
    case A_FUNC: case A_STRUCT: case A_PARAM: case A_FIELD:
    case A_VAR: case A_CONST: case A_NAME: case A_MEMBER:
    case A_TYPE_NAME: case A_TYPE_ARRAY: case A_INT: case A_STRING:
    case A_BINARY: case A_UNARY: case A_ASSIGN:
        return 1;
    default: return 0;
    }
}
static int tr_is_tokkind(AstKind k) {
    return k == A_BINARY || k == A_UNARY || k == A_ASSIGN;
}
/* Arena allocation order is not AST traversal order: comparing node indices
 * rejects equivalent trees when a parent is allocated after its children.
 * Compare paired nodes by ordered child traversal, never raw node IDs. */
typedef struct { u32 left, right; } TreePair;
static int trees_match(const Parser *a, u32 ra, const Parser *b, u32 rb) {
    TreePair todo[CAP];
    u8 visited_a[CAP] = {0}, visited_b[CAP] = {0};
    u32 top = 0, checked = 0;
    if (!a || !b || !a->nodes || !b->nodes ||
        !ra || !rb || a->count != b->count || a->count > CAP ||
        ra >= a->count || rb >= b->count) return 0;
    todo[top++] = (TreePair){ra, rb};
    while (top) {
        TreePair pair = todo[--top];
        u32 ac, bc, last_a = 0, last_b = 0;
        const AstNode *an, *bn;
        if (!pair.left || !pair.right ||
            pair.left >= a->count || pair.right >= b->count ||
            visited_a[pair.left] || visited_b[pair.right]) return 0;
        visited_a[pair.left] = visited_b[pair.right] = 1;
        checked++;
        an = &a->nodes[pair.left];
        bn = &b->nodes[pair.right];
        if (an->kind != bn->kind) return 0;
        if (tr_is_tokkind(an->kind)) {
            if (an->tok.kind != bn->tok.kind) return 0;
        } else if (tr_has_payload(an->kind)) {
            if (an->tok.len != bn->tok.len) return 0;
            if (an->tok.len &&
                (!an->tok.start || !bn->tok.start ||
                 memcmp(an->tok.start, bn->tok.start, an->tok.len))) return 0;
            /* Numeric literals must retain their evaluated value too. */
            if ((an->kind == A_INT || an->kind == A_TYPE_ARRAY) &&
                an->tok.value != bn->tok.value) return 0;
        }
        ac = an->first;
        bc = bn->first;
        while (ac && bc) {
            if (ac >= a->count || bc >= b->count || top >= CAP) return 0;
            todo[top++] = (TreePair){ac, bc};
            last_a = ac; last_b = bc;
            ac = a->nodes[ac].next;
            bc = b->nodes[bc].next;
        }
        if (ac || bc || an->last != last_a || bn->last != last_b) return 0;
    }
    return checked == a->count - 1;
}
static void test_roundtrip(void) {
    static const char *const cases[] = {
        "دالة رئيسية() { أرجع ٤٢؛ }",
        "دالة د(س: صحيح، ص: *صحيح) { إذا (س) { أرجع؛ } }",
        "هيكل ه { س: صحيح؛ ص: [٣]*صحيح؛ }",
        "دالة د() { أرجع س | ص ^ ع & ١؛ }",
        "متغير س: صحيح = ١ + ٢ * ٣؛",
        "دالة د() { طالما (س < ١٠ && ص > ٠) { س = س + ١؛ } }",
        "دالة د() { أرجع ١ + ٢ * ٣ == ٤ && ٥؛ }",
        "دالة د() { أرجع \"س\\\"ص\\\\ع\"؛ }"
    };
    char out1[8192], out2[8192];
    u8 pbuf[8192];
    static AstNode arena2[256];
    u32 ncases = (u32)(sizeof cases / sizeof cases[0]);
    for (u32 i = 0; i < ncases; i++) {
        Parser p1, p2;
        u32 root1, root2, n1, n2;
        parser_init(&p1, arena, CAP, (const u8 *)cases[i], (u64)strlen(cases[i]));
        root1 = parser_parse(&p1);
        if (!root1) { fprintf(stderr, "FAIL roundtrip[%u] parse\n", i); failed++; continue; }
        n1 = ast_serialize(&p1, root1, out1, sizeof out1);
        if (!n1) { fprintf(stderr, "FAIL roundtrip[%u] serialize\n", i); failed++; continue; }
        root2 = ast_deserialize(&p2, arena2, 256, pbuf, sizeof pbuf, out1, n1);
        if (!root2) { fprintf(stderr, "FAIL roundtrip[%u] deserialize\n", i); failed++; continue; }
        n2 = ast_serialize(&p2, root2, out2, sizeof out2);
        if (!n2 || n2 != n1 || memcmp(out1, out2, n1) != 0) {
            fprintf(stderr, "FAIL roundtrip[%u] text mismatch\n", i); failed++; continue;
        }
        if (!trees_match(&p1, root1, &p2, root2)) {
            fprintf(stderr, "FAIL roundtrip[%u] tree mismatch\n", i); failed++;
        }
    }
}
/* Invalid serializations must fail, not silently synthesize broken ASTs. */
static void test_ast_rejection(void) {
    static AstNode other[512];
    static u8 payload[8192];
    static char deep[2048], fuzzbuf[256];
    static const char *const invalid[] = {
        "(", "(PROGRAM", "(PROGRAM) trailing",
        "(PROGRAM (NAME \"unterminated))",
        "(PROGRAM (BINARY \"INVALID\"))",
        "(PROGRAM (BINARY \"=\"))",
        "(PROGRAM (ASSIGN \"+\"))",
        "(PROGRAM (UNARY \"==\"))",
        "(PROGRAM (INT \"123x\"))",
        "(PROGRAM (INT \"nope\"))",
        "(PROGRAM (NAME \"إذا\"))",
        "(PROGRAM (NAME \"a\\q\"))",
        "(PROGRAM (NAME \"\xED\"))"
    };
    Parser p, d;
    char output[8192];
    static const char source[] = "دالة د() { أرجع ٩؛ }";
    u32 n, root, old_first, i, used;
    parser_init(&p, arena, CAP, (const u8 *)source, sizeof source - 1);
    root = parser_parse(&p);
    if (!root) { fprintf(stderr, "FAIL fixture for AST guards\n"); failed++; return; }
    n = ast_serialize(&p, root, output, sizeof output);
    if (!n || ast_serialize(&p, root, output, n - 1) ||
        ast_serialize(&p, root, NULL, n) ||
        ast_deserialize(&d, NULL, 0, payload, sizeof payload, output, n) ||
        !d.error || ast_deserialize(NULL, other, 512, payload, sizeof payload, output, n)) {
        fprintf(stderr, "FAIL AST API argument and buffer checks\n"); failed++;
    }
    for (i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        if (ast_deserialize(&d, other, 512, payload, sizeof payload,
                            invalid[i], (u32)strlen(invalid[i])) || !d.error) {
            fprintf(stderr, "FAIL invalid serialized AST %u\n", i); failed++;
        }
    }
    used = 0;
    memcpy(deep + used, "(PROGRAM", 8); used += 8;
    for (i = 0; i < 150; i++) {
        memcpy(deep + used, " (BLOCK", 7); used += 7;
    }
    for (i = 0; i < 151; i++) deep[used++] = ')';
    if (ast_deserialize(&d, other, 512, payload, sizeof payload, deep, used) ||
        !d.error) {
        fprintf(stderr, "FAIL unbounded serialized tree depth\n"); failed++;
    }
    old_first = arena[root].first;
    arena[root].first = root; /* Cannot serialize cyclic AST graphs. */
    if (ast_serialize(&p, root, output, sizeof output)) {
        fprintf(stderr, "FAIL cyclic AST serialization\n"); failed++;
    }
    arena[root].first = old_first;
    {
        u32 seed = 0x334AC1u;
        for (i = 0; i < 2000; i++) {
            u32 k, size = i % sizeof fuzzbuf;
            for (k = 0; k < size; k++) {
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                fuzzbuf[k] = (char)seed;
            }
            root = ast_deserialize(&d, other, 512, payload,
                                   sizeof payload, fuzzbuf, size);
            if (root) {
                if (!ast_serialize(&d, root, output, sizeof output)) {
                    fprintf(stderr, "FAIL parsed random AST cannot serialize %u\n", i);
                    failed++;
                }
            } else if (!d.error) {
                fprintf(stderr, "FAIL fuzz AST missing error %u\n", i); failed++;
            }
        }
    }
}
static void test_priority(void) {
    static const char code[] = "دالة رئيسية() -> صحيح { أرجع ٢ + ٣ * ٤؛ }";
    Parser p;
    u32 root = parse(code, &p);
    u32 func, type, block, ret, plus, left, mult;
    if (!root || !graph_ok(&p, root)) { failed++; return; }
    func = arena[root].first;
    type = arena[func].first;
    block = arena[type].next;
    ret = arena[block].first;
    plus = arena[ret].first;
    left = arena[plus].first;
    mult = arena[left].next;
    if (arena[func].kind != A_FUNC ||
        arena[type].kind != A_TYPE_NAME || arena[block].kind != A_BLOCK ||
        arena[ret].kind != A_RETURN || arena[plus].kind != A_BINARY ||
        arena[plus].tok.kind != T_PLUS ||
        arena[left].kind != A_INT || arena[left].tok.value != 2 ||
        arena[mult].kind != A_BINARY || arena[mult].tok.kind != T_STAR) {
        fprintf(stderr, "FAIL expression precedence AST layout\n");
        failed++;
    }
}
static void test_left_associativity(void) {
    static const char code[] = "دالة د() { أرجع ٨ - ٢ - ١؛ }";
    Parser p;
    u32 root = parse(code, &p), func, block, ret, minus, left, right;
    if (!root || !graph_ok(&p, root)) { failed++; return; }
    func = arena[root].first;
    block = arena[func].first;
    ret = arena[block].first;
    minus = arena[ret].first;
    left = arena[minus].first;
    right = arena[left].next;
    if (arena[minus].kind != A_BINARY || arena[minus].tok.kind != T_MINUS ||
        arena[left].kind != A_BINARY || arena[left].tok.kind != T_MINUS ||
        arena[right].kind != A_INT || arena[right].tok.value != 1) {
        fprintf(stderr, "FAIL left associativity of subtraction\n");
        failed++;
    }
}
static void test_invalid_init(void) {
    static const u8 source[] = "دالة";
    Parser p;
    parser_init(&p, arena, CAP, 0, 0);
    if (parser_parse(&p) || !p.error) { failed++; return; }
    parser_init(&p, 0, 0, source, sizeof source - 1);
    if (parser_parse(&p) || !p.error) { failed++; return; }
    parser_init(&p, arena, CAP, source, (u64)0x100000000ULL);
    if (parser_parse(&p) || !p.error) { failed++; return; }
}
static void test_limited_arena(void) {
    static const char src[] = "دالة رئيسية() { متغير ع: صحيح = ١؛ أرجع ع؛ }";
    AstNode tiny[4];
    Parser p;
    parser_init(&p, tiny, 4, (const u8 *)src, sizeof src - 1);
    if (parser_parse(&p) || !p.error ||
        strcmp(p.error, "AST arena capacity exceeded") != 0) {
        fprintf(stderr, "FAIL AST exhaustion check\n"); failed++;
    }
}
static void test_depth(void) {
    static char src[2048];
    u32 at = 0, i;
    Parser p;
    const char start[] = "دالة د() { ";
    memcpy(src + at, start, sizeof start - 1);
    at += sizeof start - 1;
    for (i = 0; i < 200; i++) src[at++] = '{';
    for (i = 0; i < 200; i++) src[at++] = '}';
    src[at++] = '}'; src[at] = 0;
    if (parse(src, &p) || !p.error ||
        strcmp(p.error, "nesting depth exceeded") != 0) {
        fprintf(stderr, "FAIL nested block depth bounded\n"); failed++;
    }
}
static void test_else_if_depth(void) {
    static char src[8192];
    Parser p;
    u32 at = 0, i;
    static const char prefix[] = "دالة د() { ";
    static const char nested[] = "إذا (١) {} وإلا ";
    static const char suffix[] = "إذا (١) {} }";
    memcpy(src + at, prefix, sizeof prefix - 1); at += sizeof prefix - 1;
    for (i = 0; i < 200; i++) {
        memcpy(src + at, nested, sizeof nested - 1); at += sizeof nested - 1;
    }
    memcpy(src + at, suffix, sizeof suffix);
    if (parse(src, &p) || !p.error ||
        strcmp(p.error, "nesting depth exceeded") != 0) {
        fprintf(stderr, "FAIL else-if chain depth bounded\n"); failed++;
    }
}
static void fuzz(void) {
    static u8 source[240];
    u32 seed = 0x735AC1u, trial, i;
    Parser p;
    for (trial = 0; trial < 3000; trial++) {
        u32 n = trial % (u32)sizeof source;
        for (i = 0; i < n; i++) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            source[i] = (u8)seed;
        }
        parser_init(&p, arena, CAP, source, n);
        if (parser_parse(&p)) {
            if (!graph_ok(&p, 1)) {
                fprintf(stderr, "FAIL fuzz AST graph case %u\n", trial); failed++;
            }
        } else if (!p.error) {
            fprintf(stderr, "FAIL fuzz error absent case %u\n", trial); failed++;
        }
    }
}
int main(void) {
    Parser p;
    static const u8 empty[] = "";
    ok("empty", "");
    ok("example", "دالة رئيسية(س: صحيح، ص: صحيح) -> صحيح {"
       " متغير ع: صحيح = ٤٢؛"
       " إذا (ع == 0x2A && س != ١) { أرجع -1; } وإلا { أرجع 0; }"
       " طالما (ع >= 0) { ع = ع - 1; } }"
       " هيكل نقطة { x: صحيح; }");
    ok("array pointer and declarations", "هيكل نقطة { س: صحيح؛ ص: صحيح؛ } "
       "متغير ع: [٣]*صحيح؛ ثابت ب: صحيح = ٧؛");
    ok("nested flow and calls", "دالة س(أ: صحيح، ب: *صحيح) {"
       " إذا (أ) { ب[٠] = ٣؛ } وإلا إذا (ب != ٠) { أرجع؛ }"
       " وإلا { أرجع ب[٠]؛ } طالما (أ) { أ = أ - ١؛ } }");
    ok("postfix", "دالة د() { س(١،٢)[٣].حقل؛ }");
    ok("unary and priority", "دالة د() { أرجع ~-*&س + ١ << ٢ == ٤ && س؛ }");
    ok("empty bodies", "دالة د() {} هيكل ن {} متغير س: صحيح؛");
    /* Syntactically valid, but semantically invalid lvalue: reserved for M2. */
    ok("syntax-only contract", "دالة د(){ ١ = ٢؛ }");
    bad("no function name", "دالة () {}");
    bad("missing semicolon", "دالة د() { أرجع ١ }");
    bad("const without initializer", "ثابت س: صحيح؛");
    bad("missing type", "متغير س: ؛");
    bad("struct field semicolon", "هيكل س { ح: صحيح }");
    bad("else not block", "دالة د() { إذا (١) { أرجع؛ } وإلا أرجع؛ }");
    bad("dangling call comma", "دالة د() { س(١،)؛ }");
    bad("missing index bracket", "دالة د() { س[١؛ }");
    bad("field identifier missing", "دالة د() { س.؛ }");
    bad("unterminated nested block", "دالة د() { إذا (١) { أرجع؛ }");
    bad("top level expression", "١ + ٢؛");
    bad("missing rhs", "دالة د() { أرجع ١ + ؛ }");
    bad("bad UTF-8 in string", "دالة د() { أرجع \"\xED\"؛ }");
    bad("no hex digits", "دالة د() { أرجع 0x؛ }");
    bad("double assignment", "دالة د() { س = ص = ع؛ }");
    /* --- M1 fixture expansion: 22 additional valid cases --- */
    ok("single param", "دالة د(س: صحيح) { أرجع س؛ }");
    ok("three params", "دالة د(س: صحيح، ص: بايت، ع: منطقي) {}");
    ok("return pointer type", "دالة د() -> *صحيح { أرجع ٠؛ }");
    ok("return array type", "دالة د() -> [٣]صحيح { أرجع؛ }");
    ok("pointer to pointer", "متغير س: **صحيح؛");
    ok("array of pointer", "متغير س: [٤]*صحيح؛");
    ok("pointer to array", "متغير س: *[٤]صحيح؛");
    ok("if without else", "دالة د() { إذا (س) { أرجع؛ } }");
    ok("else-if chain deep", "دالة د() { إذا (١) {} وإلا إذا (٢) {} وإلا {} }");
    ok("while complex condition", "دالة د() { طالما (س < ١٠ && ص > ٠) { س = س + ١؛ } }");
    ok("bare return", "دالة د() { أرجع؛ }");
    ok("nested bare blocks", "دالة د() { { { أرجع؛ } } }");
    ok("bitwise operators", "دالة د() { أرجع س | ص ^ ع & ١؛ }");
    ok("shift and compare", "دالة د() { أرجع س << ٢ >= ص >> ١؛ }");
    ok("logical operators", "دالة د() { أرجع س || ص && ع؛ }");
    ok("equality chain", "دالة د() { أرجع س == ص != ع؛ }");
    ok("hex literal", "دالة د() { أرجع 0xFF؛ }");
    ok("struct array-of-pointer field", "هيكل ه { س: صحيح؛ ص: [٣]*صحيح؛ }");
    ok("top-level var init", "متغير س: صحيح = ١ + ٢ * ٣؛");
    ok("nested calls", "دالة د() { س(ص(ع))؛ }");
    ok("calls with 0..3 args", "دالة د() { س()؛ س(١)؛ س(١،٢)؛ س(١،٢،٣)؛ }");
    ok("member chain and unary", "دالة د() { س.ح.خ؛ أرجع &*س؛ }");

    /* --- M1 fixture expansion: 15 additional invalid cases --- */
    bad("param missing type", "دالة د(س) {}");
    bad("param type after colon", "دالة د(س: ) {}");
    bad("arrow without type", "دالة د() -> {}");
    bad("unterminated string", "دالة د() { أرجع \"abc؛ }");
    bad("unterminated block comment", "دالة د() { /* unclosed }");
    bad("unbalanced open paren", "دالة د() { (١؛ }");
    bad("unbalanced close paren", "دالة د() { ١)؛ }");
    bad("two expressions juxtaposed", "دالة د() { أرجع ١ ٢؛ }");
    bad("trailing comma in params", "دالة د(س: صحيح,) {}");
    bad("double equals in assignment", "دالة د() { س = = ١؛ }");
    bad("struct field missing type", "هيكل ه { س }");
    bad("incomplete struct at EOF", "دالة د() {} هيكل");
    bad("const empty initializer", "ثابت س: صحيح = ؛");
    bad("var empty initializer", "متغير س: صحيح = ؛");
    bad("empty while condition", "دالة د() { طالما () {} }");
    bad_multi("two missing semicolons",
              "دالة د() { أرجع ١ } دالة ه() { أرجع ٢ }", 2);
    bad_multi("two missing names",
              "دالة () {} دالة () {}", 2);
    test_roundtrip();
    test_ast_rejection();
    test_priority();
    test_left_associativity();
    test_invalid_init();
    test_limited_arena();
    test_depth();
    test_else_if_depth();
    fuzz();
    parser_init(&p, arena, CAP, empty, 0);
    if (!parser_parse(&p) || p.error) failed++;
    if (failed) return 1;
    puts("parser_test: 30 valid, 30 invalid, 2 multi-error, 8 roundtrip, 2000 serialized byte cases, AST hardening, AST invariants, 3000 byte cases PASS");
    return 0;
}
