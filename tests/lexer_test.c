#include "../src/lexer.h"

static int bytes_eq(const Token *t, const u8 *s, u64 len) {
    u64 i;
    if (t->len != len) return 0;
    for (i = 0; i < len; i++) {
        if (t->start[i] != s[i]) return 0;
    }
    return 1;
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
    static const u8 keyword[] = { 0xD8,0xA5,0xD8,0xB0,0xD8,0xA7 };
    static const u8 unterminated_comment[] = { '/', '*', 'x' };
    static const u8 invalid_string_utf8[] = { '"', 'a', 0xED, 0xA0, 0x80, '"' };
    static const u8 invalid_line_comment[] = { '/', '/', 0xED, 0xA0, 0x80, '\n', 'x' };
    static const u8 invalid_block_comment[] = { '/', '*', 0xED, 0xA0, 0x80, '*', '/', 'x' };
    static const u8 arabic_line_comment[] = "// هذا تعليق\nإذا";
    static const u8 arabic_string[] = "\"مرحبا\"";
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
    if (t.kind != T_IDENT || !bytes_eq(&t, prefix, sizeof prefix)) return 6;
    if (lex_next(&l).kind != T_EOF) return 7;
    if (!one(keyword, sizeof keyword, K_IF, 0)) return 8;

    lex_init(&l, unterminated_comment, sizeof unterminated_comment);
    t = lex_next(&l);
    if (t.kind != T_ERROR || t.value != 1) return 9;
    if (lex_next(&l).kind != T_EOF) return 10;
    if (lex_next(&l).kind != T_EOF) return 11;
    if (!one(invalid_string_utf8, sizeof invalid_string_utf8, T_ERROR, 6)) return 12;
    if (!one(invalid_line_comment, sizeof invalid_line_comment, T_ERROR, 6)) return 13;
    if (!one(invalid_block_comment, sizeof invalid_block_comment, T_ERROR, 6)) return 14;
    if (!one(arabic_line_comment, sizeof arabic_line_comment - 1, K_IF, 0)) return 15;
    if (!one(arabic_string, sizeof arabic_string - 1, T_STRING, 0)) return 16;
    /* The spelling is ثابت (tha-alif-ba-ta), not ثاتب. */
    if (!one((const u8 *)"ثابت", sizeof "ثابت" - 1, K_CONST, 0)) return 17;
    if (!one((const u8 *)"ثاتب", sizeof "ثاتب" - 1, T_IDENT, 0)) return 18;
    if (!one((const u8 *)"ثابتة", sizeof "ثابتة" - 1, T_IDENT, 0)) return 19;

    return 0;
}
