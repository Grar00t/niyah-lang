#include "../src/lexer.h"

static int bytes_eq(const Token *t, const char *s) {
    u32 i = 0;
    while (s[i]) {
        if (i >= t->len || t->start[i] != (u8)s[i]) return 0;
        i++;
    }
    return i == t->len;
}

static int one(const u8 *src, u64 len, TokKind kind, i64 value) {
    Lexer l;
    Token t;
    lex_init(&l, src, len);
    t = lex_next(&l);
    return t.kind == kind && t.value == value;
}

int main(void) {
    static const u8 bad_surrogate[] = { 0xED, 0xA0, 0x80 };
    static const u8 bad_string[] = { '"', 'a', '\\', 'q', '"' };
    static const u8 max_hex[] = "0x7FFFFFFFFFFFFFFF";
    static const u8 overflow_hex[] = "0x8000000000000000";
    static const u8 prefix[] = { 0xD8,0xA5,0xD8,0xB0,0xD8,0xA7,0xD9,0x86 };
    Lexer l;
    Token t;

    if (!one(bad_surrogate, sizeof bad_surrogate, T_ERROR, 6)) return 1;
    if (!one(max_hex, sizeof max_hex - 1, T_INT, 0x7FFFFFFFFFFFFFFFL)) return 2;
    if (!one(overflow_hex, sizeof overflow_hex - 1, T_ERROR, 4)) return 3;

    lex_init(&l, bad_string, sizeof bad_string);
    t = lex_next(&l);
    if (t.kind != T_ERROR || t.value != 3 || t.len != sizeof bad_string) return 4;
    if (lex_next(&l).kind != T_EOF) return 5;

    lex_init(&l, prefix, sizeof prefix);
    t = lex_next(&l);
    if (t.kind != T_IDENT || !bytes_eq(&t, (const char *)prefix)) return 6;

    return 0;
}
