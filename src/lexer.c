/* Niyah stage0 lexer. Freestanding: no libc, raw syscalls. UTF-8 input. */
#include "lexer.h"

static const char *const NAMES[T_COUNT] = {
    "EOF", "ERROR", "IDENT", "INT", "STRING",
    "IF", "ELSE", "WHILE", "RETURN", "VAR", "CONST", "FUNC", "STRUCT",
    "(", ")", "{", "}", "[", "]", ",", ";", ":", ".", "->",
    "+", "-", "*", "/", "%", "&", "|", "^", "~", "!",
    "=", "==", "!=", "<", "<=", ">", ">=", "<<", ">>", "&&", "||"
};
static const char *const ERRS[] = {
    "ok", "unterminated block comment", "unterminated string",
    "bad escape", "integer overflow", "bad hex literal",
    "invalid UTF-8 or unexpected character"
};
enum { E_COMMENT = 1, E_STR, E_ESC, E_OVF, E_HEX, E_CHAR };

static const struct { const char *s; TokKind k; } KW[] = {
    { "\xd8\xa5\xd8\xb0\xd8\xa7", K_IF },                 /* إذا */
    { "\xd9\x88\xd8\xa5\xd9\x84\xd8\xa7", K_ELSE },       /* وإلا */
    { "\xd8\xb7\xd8\xa7\xd9\x84\xd9\x85\xd8\xa7", K_WHILE }, /* طالما */
    { "\xd8\xa3\xd8\xb1\xd8\xac\xd8\xb9", K_RETURN },     /* أرجع */
    { "\xd9\x85\xd8\xaa\xd8\xba\xd9\x8a\xd8\xb1", K_VAR },   /* متغير */
    { "\xd8\xab\xd8\xa7\xd8\xaa\xd8\xa8", K_CONST },      /* ثابت */
    { "\xd8\xaf\xd8\xa7\xd9\x84\xd8\xa9", K_FUNC },       /* دالة */
    { "\xd9\x87\xd9\x8a\xd9\x83\xd9\x84", K_STRUCT },     /* هيكل */
};

const char *tok_name(TokKind k) { return k < T_COUNT ? NAMES[k] : "?"; }
const char *lex_errmsg(i64 id) { return ERRS[id]; }

void lex_init(Lexer *L, const u8 *src, u64 len) {
    L->cur = src; L->end = src + len; L->line = 1; L->col = 1;
    if (len >= 3 && src[0] == 0xEF && src[1] == 0xBB && src[2] == 0xBF) L->cur += 3;
}

/* Decode one code point at p. *n = bytes consumed (0 at EOF). Invalid -> 0xFFFFFFFF, n=1. */
static u32 dec(const u8 *p, const u8 *e, u32 *n) {
    u32 c;
    if (p >= e) { *n = 0; return 0; }
    c = p[0];
    *n = 1;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && e - p >= 2 && (p[1] & 0xC0) == 0x80 && c >= 0xC2) {
        *n = 2; return ((c & 0x1F) << 6) | (p[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && e - p >= 3 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        u32 v = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        if (v < 0x800 || (v >= 0xD800 && v <= 0xDFFF)) return 0xFFFFFFFF;
        *n = 3; return v;
    }
    if ((c & 0xF8) == 0xF0 && e - p >= 4 && (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        u32 v = ((c & 7) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        if (v < 0x10000 || v > 0x10FFFF) return 0xFFFFFFFF;
        *n = 4; return v;
    }
    return 0xFFFFFFFF;
}

static int is_alpha(u32 c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           (c >= 0x0620 && c <= 0x064A) || (c >= 0x066E && c <= 0x06D3) ||
           (c >= 0x06FA && c <= 0x06FF);
}
/* Arabic-Indic (U+0660..0669) and Extended (U+06F0..06F9) digits -> 0..9, else -1 */
static int digit_val(u32 c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 0x0660 && c <= 0x0669) return (int)(c - 0x0660);
    if (c >= 0x06F0 && c <= 0x06F9) return (int)(c - 0x06F0);
    return -1;
}
static int is_mark(u32 c) { return (c >= 0x064B && c <= 0x065F) || c == 0x0670; } /* harakat, shadda */
static int is_alnum(u32 c) { return is_alpha(c) || digit_val(c) >= 0 || is_mark(c); }

static u32 peekc(const Lexer *L, u32 *n) { return dec(L->cur, L->end, n); }
static void adv(Lexer *L, u32 n) {
    while (n--) {
        u8 b = *L->cur++;
        if (b == '\n') { L->line++; L->col = 1; }
        else if ((b & 0xC0) != 0x80) L->col++;
    }
}
static int peek_byte(const Lexer *L, u32 off) {
    return (L->end - L->cur > (long)off) ? L->cur[off] : -1;
}

static Token mk(const Lexer *L, TokKind k, const u8 *s, u32 line, u32 col, i64 v) {
    Token t;
    t.kind = k; t.line = line; t.col = col; t.start = s; t.len = (u32)(L->cur - s); t.value = v;
    return t;
}

/* returns 0 ok, or error id (unterminated comment) */
static int skip_trivia(Lexer *L) {
    for (;;) {
        u32 n, c = peekc(L, &n);
        if (n == 0) return 0;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0x200F || c == 0x200E ||
            c == 0x061C) { adv(L, n); continue; }     /* RLM/LRM/ALM are whitespace */
        if (c == '/' && peek_byte(L, 1) == '/') {
            while (L->cur < L->end && *L->cur != '\n') adv(L, 1);
            continue;
        }
        if (c == '/' && peek_byte(L, 1) == '*') {
            adv(L, 2);
            for (;;) {
                if (L->cur >= L->end) return E_COMMENT;
                if (*L->cur == '*' && peek_byte(L, 1) == '/') { adv(L, 2); break; }
                adv(L, 1);
            }
            continue;
        }
        return 0;
    }
}

static Token lex_ident(Lexer *L, u32 line, u32 col) {
    const u8 *s = L->cur;
    u32 n, c;
    unsigned i;
    while ((c = peekc(L, &n)), n && is_alnum(c)) adv(L, n);
    for (i = 0; i < sizeof KW / sizeof KW[0]; i++) {
        const u8 *a = s; const char *b = KW[i].s;
        while (a < L->cur && *b && *a == (u8)*b) { a++; b++; }
        if (a == L->cur && *b == 0) return mk(L, KW[i].k, s, line, col, 0);
    }
    return mk(L, T_IDENT, s, line, col, 0);
}

static Token lex_number(Lexer *L, u32 line, u32 col) {
    const u8 *s = L->cur;
    u64 v = 0; u32 n, c; int d, any = 0;
    if (*L->cur == '0' && (peek_byte(L, 1) == 'x' || peek_byte(L, 1) == 'X')) {
        adv(L, 2);
        for (;;) {
            int b = peek_byte(L, 0);
            d = (b >= '0' && b <= '9') ? b - '0' : (b >= 'a' && b <= 'f') ? b - 'a' + 10 :
                (b >= 'A' && b <= 'F') ? b - 'A' + 10 : -1;
            if (d < 0) break;
            if (v > (0x7FFFFFFFFFFFFFFFUL - (u64)d) / 16) { adv(L, 1); return mk(L, T_ERROR, s, line, col, E_OVF); }
            v = (v << 4) | (u64)d; any = 1; adv(L, 1);
        }
        if (!any) return mk(L, T_ERROR, s, line, col, E_HEX);
        return mk(L, T_INT, s, line, col, (i64)v);
    }
    while ((c = peekc(L, &n)), n && (d = digit_val(c)) >= 0) {
        if (v > (0x7FFFFFFFFFFFFFFFUL - (u64)d) / 10) { adv(L, n); return mk(L, T_ERROR, s, line, col, E_OVF); }
        v = v * 10 + (u64)d; adv(L, n);
    }
    return mk(L, T_INT, s, line, col, (i64)v);
}

static Token lex_string(Lexer *L, u32 line, u32 col) {
    const u8 *s = L->cur;
    adv(L, 1);
    for (;;) {
        int b = peek_byte(L, 0);
        if (b < 0 || b == '\n') return mk(L, T_ERROR, s, line, col, E_STR);
        if (b == '"') { adv(L, 1); return mk(L, T_STRING, s, line, col, 0); }
        if (b == '\\') {
            int e = peek_byte(L, 1);
            if (e != 'n' && e != 't' && e != 'r' && e != '0' && e != '\\' && e != '"') {
                adv(L, 1);
                while ((e = peek_byte(L, 0)) >= 0 && e != '\n' && e != '"') adv(L, 1);
                if (e == '"') adv(L, 1);
                return mk(L, T_ERROR, s, line, col, E_ESC);
            }
            adv(L, 2); continue;
        }
        adv(L, 1);
    }
}

Token lex_next(Lexer *L) {
    const u8 *s; u32 n, c, line, col; int e, b1;
    if ((e = skip_trivia(L)) != 0) return mk(L, T_ERROR, L->cur, L->line, L->col, e);
    line = L->line; col = L->col; s = L->cur;
    c = peekc(L, &n);
    if (n == 0) return mk(L, T_EOF, s, line, col, 0);
    if (is_alpha(c)) return lex_ident(L, line, col);
    if (digit_val(c) >= 0) return lex_number(L, line, col);
    if (c == '"') return lex_string(L, line, col);
    if (c == 0x060C) { adv(L, n); return mk(L, T_COMMA, s, line, col, 0); }  /* ، */
    if (c == 0x061B) { adv(L, n); return mk(L, T_SEMI, s, line, col, 0); }   /* ؛ */
    if (c >= 0x80) { adv(L, n); return mk(L, T_ERROR, s, line, col, E_CHAR); }
    b1 = peek_byte(L, 1);
    adv(L, 1);
#define K1(ch, k) case ch: return mk(L, k, s, line, col, 0)
#define K2(ch, nx, k2, k1) case ch: if (b1 == nx) { adv(L, 1); return mk(L, k2, s, line, col, 0); } \
                                    return mk(L, k1, s, line, col, 0)
    switch (c) {
    K1('(', T_LPAREN); K1(')', T_RPAREN); K1('{', T_LBRACE); K1('}', T_RBRACE);
    K1('[', T_LBRACK); K1(']', T_RBRACK); K1(',', T_COMMA); K1(';', T_SEMI);
    K1(':', T_COLON); K1('.', T_DOT); K1('+', T_PLUS); K1('*', T_STAR);
    K1('/', T_SLASH); K1('%', T_PERCENT); K1('^', T_CARET); K1('~', T_TILDE);
    K2('-', '>', T_ARROW, T_MINUS); K2('=', '=', T_EQ, T_ASSIGN); K2('!', '=', T_NE, T_BANG);
    K2('&', '&', T_ANDAND, T_AMP); K2('|', '|', T_OROR, T_PIPE);
    case '<':
        if (b1 == '=') { adv(L, 1); return mk(L, T_LE, s, line, col, 0); }
        if (b1 == '<') { adv(L, 1); return mk(L, T_SHL, s, line, col, 0); }
        return mk(L, T_LT, s, line, col, 0);
    case '>':
        if (b1 == '=') { adv(L, 1); return mk(L, T_GE, s, line, col, 0); }
        if (b1 == '>') { adv(L, 1); return mk(L, T_SHR, s, line, col, 0); }
        return mk(L, T_GT, s, line, col, 0);
    }
    return mk(L, T_ERROR, s, line, col, E_CHAR);
}

#ifdef NIYAH_LEXER_STANDALONE
/* ---- raw syscalls, no libc ---- */
static i64 sys3(i64 nr, i64 a, i64 b, i64 c) {
    i64 r;
    __asm__ volatile ("syscall" : "=a"(r) : "a"(nr), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return r;
}
#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_exit 231

static u8 srcbuf[1 << 22];
static char obuf[1 << 16];
static u32 olen;
static void oflush(void) {
    u32 off = 0;
    while (off < olen) { i64 w = sys3(SYS_write, 1, (i64)(obuf + off), olen - off); if (w <= 0) break; off += (u32)w; }
    olen = 0;
}
static void oput(const u8 *s, u32 n) { while (n--) { if (olen == sizeof obuf) oflush(); obuf[olen++] = (char)*s++; } }
static void ostr(const char *s) { const char *p = s; while (*p) p++; oput((const u8 *)s, (u32)(p - s)); }
static void onum(i64 v) {
    char t[24]; int i = 24; u64 u = v < 0 ? (u64)0 - (u64)v : (u64)v;
    do { t[--i] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) t[--i] = '-';
    oput((const u8 *)(t + i), (u32)(24 - i));
}

__attribute__((noreturn, used)) static void niyah_main(long argc, char **argv) {
    static const char dflt[] = "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7.\xd9\x86\xd9\x8a\xd9\x91\xd8\xa9"; /* مرحبا.نيّة */
    const char *path = argc > 1 ? argv[1] : dflt;
    i64 fd = sys3(SYS_open, (i64)path, 0, 0), total = 0, r;
    int errs = 0;
    Lexer L; Token t;
    if (fd < 0) { ostr("error: cannot open "); ostr(path); ostr("\n"); oflush(); sys3(SYS_exit, 1, 0, 0); }
    while ((r = sys3(SYS_read, fd, (i64)(srcbuf + total), (i64)sizeof srcbuf - total)) > 0) total += r;
    lex_init(&L, srcbuf, (u64)total);
    do {
        t = lex_next(&L);
        onum(t.line); ostr(":"); onum(t.col); ostr("\t"); ostr(tok_name(t.kind));
        if (t.kind == T_IDENT || t.kind == T_STRING || t.kind == T_INT || t.kind == T_ERROR ||
            (t.kind >= K_IF && t.kind <= K_STRUCT)) { ostr("\t"); oput(t.start, t.len); }
        if (t.kind == T_INT) { ostr("\t="); onum(t.value); }
        if (t.kind == T_ERROR) { ostr("\t"); ostr(lex_errmsg(t.value)); errs++; }
        ostr("\n");
    } while (t.kind != T_EOF);
    oflush();
    sys3(SYS_exit, errs ? 2 : 0, 0, 0);
    for (;;) {}
}
__asm__(".text\n.globl _start\n_start:\n xor %ebp,%ebp\n mov (%rsp),%rdi\n lea 8(%rsp),%rsi\n"
        " and $-16,%rsp\n call niyah_main\n ud2\n");
#endif
