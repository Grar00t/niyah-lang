#include "../src/lexer.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    TokKind kind;
    i64 error;
    u32 line;
    u32 col;
    u32 minimum_len;
} Want;

static int check(const char *name, const u8 *s, u64 n, const Want *w, u32 count) {
    Lexer l;
    u32 i;
    lex_init(&l, s, n);
    for (i = 0; i < count; i++) {
        Token t = lex_next(&l);
        if (t.kind != w[i].kind || (t.kind == T_ERROR && t.value != w[i].error) ||
            (w[i].line && t.line != w[i].line) ||
            (w[i].col && t.col != w[i].col) ||
            (t.len < w[i].minimum_len)) {
            fprintf(stderr, "FAIL %s token[%u]: got kind=%d value=%ld at=%u:%u len=%u; expected kind=%d value=%ld at=%u:%u minlen=%u\n",
                    name, i, t.kind, t.value, t.line, t.col, t.len,
                    w[i].kind, w[i].error, w[i].line, w[i].col, w[i].minimum_len);
            return 0;
        }
    }
    return lex_next(&l).kind == T_EOF;
}
#define RUN(name, str, ...) do { \
    static const u8 src[] = str; \
    static const Want want[] = { __VA_ARGS__ }; \
    if (!check(name, src, sizeof src-1, want, sizeof want / sizeof want[0])) failures++; \
} while (0)

static int fixed_seed_fuzz(void) {
    static u8 src[256];
    u32 seed = 0x9E3779B9u, case_id, j;
    for (case_id = 0; case_id < 5000; case_id++) {
        const u32 len = case_id % (u32)sizeof src;
        Lexer lex;
        const u8 *last = src;
        u32 steps = 0;
        for (j = 0; j < len; j++) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            src[j] = (u8)seed;
        }
        lex_init(&lex, src, len);
        for (;;) {
            Token t = lex_next(&lex);
            if (t.start < src || t.start > src + len ||
                t.len > (u64)(src + len - t.start)) {
                fprintf(stderr, "fuzz invalid span case=%u step=%u\n", case_id, steps);
                return 0;
            }
            if (t.kind == T_EOF) {
                if (t.start != src + len || t.len != 0) {
                    fprintf(stderr, "fuzz bad EOF case=%u\n", case_id);
                    return 0;
                }
                break;
            }
            if (t.len == 0 || lex.cur <= last || ++steps > len + 1) {
                fprintf(stderr, "fuzz no progress case=%u step=%u kind=%d\n",
                        case_id, steps, t.kind);
                return 0;
            }
            last = lex.cur;
        }
    }
    return 1;
}

int main(void) {
    int failures = 0;
    /* Every malformed quoted/comment region must produce one error, never leak IF. */
    RUN("bad-string-utf8", "\"a" "\xED" "إذا\" أرجع",
        {T_ERROR, 6, 1, 1, 7}, {K_RETURN, 0, 1, 0, 1}, {T_EOF, 0, 1, 0, 0});
    RUN("bad-string-escaped-quote", "\"a" "\xED" "\\\"إذا\" أرجع",
        {T_ERROR, 6, 1, 1, 8}, {K_RETURN, 0, 1, 0, 1}, {T_EOF, 0, 1, 0, 0});
    RUN("bad-block-comment-utf8", "/* " "\xED" " إذا */ أرجع",
        {T_ERROR, 6, 1, 1, 10}, {K_RETURN, 0, 1, 0, 1}, {T_EOF, 0, 1, 0, 0});
    RUN("bad-line-comment-utf8", "// " "\xED" " إذا\nأرجع",
        {T_ERROR, 6, 1, 1, 7}, {K_RETURN, 0, 2, 1, 1}, {T_EOF, 0, 2, 0, 0});
    RUN("bad-escape-string", "\"a\\qإذا\" أرجع",
        {T_ERROR, 3, 1, 1, 7}, {K_RETURN, 0, 1, 0, 1}, {T_EOF, 0, 1, 0, 0});
    RUN("unterminated-comment", "/* إذا",
        {T_ERROR, 1, 1, 1, 2}, {T_EOF, 0, 1, 0, 0});
    RUN("decimal-overflow-tail", "9223372036854775808000123+٢",
        {T_ERROR, 4, 1, 1, 25}, {T_PLUS, 0, 1, 0, 1},
        {T_INT, 0, 1, 0, 1}, {T_EOF, 0, 1, 0, 0});
    RUN("hex-overflow-tail", "0x8000000000000000ABCD+٢",
        {T_ERROR, 4, 1, 1, 22}, {T_PLUS, 0, 1, 0, 1},
        {T_INT, 0, 1, 0, 1}, {T_EOF, 0, 1, 0, 0});
    RUN("cr-only-lines", "أرجع\rإذا",
        {K_RETURN, 0, 1, 1, 1}, {K_IF, 0, 2, 1, 1}, {T_EOF, 0, 2, 0, 0});
    RUN("crlf-lines", "أرجع\r\nإذا",
        {K_RETURN, 0, 1, 1, 1}, {K_IF, 0, 2, 1, 1}, {T_EOF, 0, 2, 0, 0});
    RUN("line-comment-cr", "// تعليق\rإذا",
        {K_IF, 0, 2, 1, 1}, {T_EOF, 0, 2, 0, 0});
    RUN("valid-comment-followed-by-code", "/* إذا */أرجع",
        {K_RETURN, 0, 1, 0, 1}, {T_EOF, 0, 1, 0, 0});
    RUN("normal-utf8-string", "\"مرحبا\" أرجع",
        {T_STRING, 0, 1, 1, 1}, {K_RETURN, 0, 1, 0, 1},
        {T_EOF, 0, 1, 0, 0});
    if (!fixed_seed_fuzz()) failures++;
    if (failures) {
        fprintf(stderr, "lexer_recovery_test: %d failing scenarios\n", failures);
        return 1;
    }
    puts("lexer_recovery_test: 13 full-stream scenarios and 5000 fuzz cases PASS");
    return 0;
}
