/* Niyah M1 CLI. Linux x86-64 ELF, no libc, raw syscalls. */
#include "parser.h"
#include "check.h"

enum { SYS_READ = 0, SYS_WRITE = 1, SYS_OPEN = 2, SYS_EXIT_GROUP = 231 };
enum { SOURCE_CAP = 1 << 22, AST_CAP = 16384, OUT_CAP = 16384 };
static u8 source[SOURCE_CAP];
static AstNode ast[AST_CAP];
static Checker semantic;
static char out[OUT_CAP];
static u32 outlen;

static i64 syscall3(i64 nr, i64 a, i64 b, i64 c) {
    i64 ret;
    __asm__ volatile ("syscall" : "=a"(ret) :
                      "a"(nr), "D"(a), "S"(b), "d"(c) :
                      "rcx", "r11", "cc", "memory");
    return ret;
}
__attribute__((noreturn)) static void quit(int code) {
    syscall3(SYS_EXIT_GROUP, code, 0, 0);
    __builtin_trap(); /* Fail hard if the normally noreturn syscall unexpectedly returns. */
}
static void flush(void) {
    u32 used = 0;
    while (used < outlen) {
        i64 n = syscall3(SYS_WRITE, 1, (i64)(out + used), outlen - used);
        if (n <= 0) quit(3);
        used += (u32)n;
    }
    outlen = 0;
}
static void put(const u8 *s, u32 len) {
    while (len--) {
        if (outlen >= OUT_CAP) flush();
        out[outlen++] = (char)*s++;
    }
}
static void str(const char *s) {
    const char *end = s;
    while (*end) end++;
    put((const u8 *)s, (u32)(end - s));
}
static void num(i64 v) {
    char b[24];
    u32 i = sizeof b;
    u64 n = v < 0 ? (u64)0 - (u64)v : (u64)v;
    do {
        b[--i] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    if (v < 0) b[--i] = '-';
    put((const u8 *)(b + i), (u32)(sizeof b - i));
}
static int equals(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}
typedef struct { u32 id, depth; } Frame;
static Frame stack[AST_CAP];
static void dump(const Parser *p, u32 root) {
    u32 sp = 0;
    stack[sp++] = (Frame){root, 0};
    while (sp) {
        Frame f = stack[--sp];
        const AstNode *n = &p->nodes[f.id];
        u32 i, amount = f.depth * 2;
        if (n->next) {
            if (sp >= AST_CAP) quit(3);
            stack[sp++] = (Frame){n->next, f.depth};
        }
        if (n->first) {
            if (sp >= AST_CAP) quit(3);
            stack[sp++] = (Frame){n->first, f.depth + 1};
        }
        if (amount > 200) amount = 200;
        for (i = 0; i < amount; i++) str(" ");
        str(ast_name(n->kind)); str(" @");
        num(n->tok.line); str(":"); num(n->tok.col);
        if (n->kind == A_NAME || n->kind == A_FUNC || n->kind == A_STRUCT ||
            n->kind == A_VAR || n->kind == A_CONST || n->kind == A_PARAM ||
            n->kind == A_FIELD || n->kind == A_TYPE_NAME || n->kind == A_MEMBER ||
            n->kind == A_INT || n->kind == A_STRING || n->kind == A_TYPE_ARRAY) {
            str(" ");
            put(n->tok.start, n->tok.len);
        } else if (n->kind == A_UNARY || n->kind == A_BINARY ||
                   n->kind == A_ASSIGN) {
            str(" "); str(tok_name(n->tok.kind));
        }
        str("\n");
    }
}
__attribute__((noreturn, used)) static void entry(long argc, char **argv) {
    int show_ast = 0, check_types = 0;
    const char *file;
    i64 fd, total = 0, got = 0;
    Parser parser;
    u32 root;
    if (argc == 3 && equals(argv[1], "--ast")) {
        show_ast = 1; file = argv[2];
    } else if (argc == 3 && equals(argv[1], "--check")) {
        check_types = 1; file = argv[2];
    } else if (argc == 2 && argv[1][0] != '-') {
        file = argv[1];
    } else {
        str("usage: niyahparse [--ast|--check] FILE\n"); flush(); quit(1);
    }
    fd = syscall3(SYS_OPEN, (i64)file, 0, 0);
    if (fd < 0) {
        str("error: cannot open "); str(file); str("\n");
        flush(); quit(1);
    }
    while (total < SOURCE_CAP) {
        got = syscall3(SYS_READ, fd, (i64)(source + total), SOURCE_CAP - total);
        if (got <= 0) break;
        total += got;
    }
    if (got < 0) {
        str("error: source read failed\n"); flush(); quit(1);
    }
    if (total == SOURCE_CAP) {
        u8 extra;
        got = syscall3(SYS_READ, fd, (i64)&extra, 1);
        if (got != 0) {
            str(got > 0 ? "error: input too large (4194304 bytes max)\n" :
                         "error: source read failed\n");
            flush(); quit(1);
        }
    }
    parser_init(&parser, ast, AST_CAP, source, (u64)total);
    root = parser_parse(&parser);
    if (!root) {
        str("error:");
        num(parser.error_at.line); str(":"); num(parser.error_at.col);
        str(": "); str(parser.error ? parser.error : "unknown parse error");
        if (parser.error_at.kind == T_ERROR) {
            str(" ("); str(lex_errmsg(parser.error_at.value)); str(")");
        } else {
            str(" (got "); str(tok_name(parser.error_at.kind)); str(")");
        }
        str("\n"); flush(); quit(2);
    }
    if (check_types) {
        checker_init(&semantic, &parser);
        if (!checker_run(&semantic, root)) {
            str("type:"); num(semantic.at.line); str(":");
            num(semantic.at.col); str(": ");
            str(semantic.message ? semantic.message : "unsupported semantic form");
            str("\n"); flush(); quit(4);
        }
        str("OK M2 checked_nodes="); num(semantic.checked_nodes);
        str("\n"); flush(); quit(0);
    }
    str("OK M1 nodes="); num(parser.count - 1); str("\n");
    if (show_ast) dump(&parser, root);
    flush(); quit(0);
}
__asm__(".text\n.globl _start\n_start:\n xor %ebp,%ebp\n mov (%rsp),%rdi\n lea 8(%rsp),%rsi\n"
        " and $-16,%rsp\n call entry\n ud2\n");
