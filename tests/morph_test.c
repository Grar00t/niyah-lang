#include "../src/morph.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #c); return 1; } } while (0)

static int valid_parts(const u8 *s, u32 len, const MorphResult *r) {
    u32 i, j;
    (void)s;
    if (r->count > MORPH_MAX_ANALYSES) return 0;
    for (i = 0; i < r->count; ++i) {
        const MorphAnalysis *a = &r->analyses[i];
        u32 next = 0;
        if (a->count > MORPH_MAX_PARTS || (a->root_len != 0 && a->root_len != 3)) return 0;
        for (j = 0; j < a->count; ++j) {
            const MorphPart *p = &a->parts[j];
            if (!p->len || p->offset != next || p->len > len - next) return 0;
            next += p->len;
        }
        if (next != len) return 0;
    }
    return 1;
}

static int has_chain(const MorphResult *r, const MorphId *ids, u32 n) {
    u32 i, j;
    for (i = 0; i < r->count; ++i) {
        const MorphAnalysis *a = &r->analyses[i];
        if (a->count != n) continue;
        for (j = 0; j < n && a->parts[j].id == ids[j]; ++j) {}
        if (j == n) return 1;
    }
    return 0;
}

static int has_pattern_root(const MorphResult *r, MorphPattern pattern,
                            u32 a, u32 b, u32 c) {
    u32 i;
    for (i = 0; i < r->count; ++i) {
        const MorphAnalysis *m = &r->analyses[i];
        if (m->pattern == pattern && m->root_len == 3 &&
            m->root[0] == a && m->root[1] == b && m->root[2] == c) return 1;
    }
    return 0;
}

static int analyze(const char *s, MorphResult *r) {
    u32 len = (u32)strlen(s);
    MorphStatus status = morph_analyze((const u8 *)s, len, r);
    if (status != MORPH_OK || !r->count || !valid_parts((const u8 *)s, len, r)) return 0;
    return r->analyses[0].count == (len ? 1u : 0u) &&
        (len == 0 || (r->analyses[0].parts[0].len == len &&
                      r->analyses[0].parts[0].id == M_STEM));
}

int main(void) {
    static MorphResult r;
    static const MorphId article_chain[] = { M_CONJ_W, M_DET_AL, M_STEM };
    static const MorphId verb_chain[] = { M_FUTURE_S, M_IMPF_Y, M_STEM };
    static const MorphId ascii_chain[] = { M_DET_AL, M_STEM };
    static const u8 invalid[] = { 0xED, 0xA0, 0x80 };
    static const u8 overlong[] = { 0xC0, 0xAF };
    static const u8 invalid_cont[] = { 0xE2, 0x28, 0xA1 };
    u8 long_word[2 * (MORPH_MAX_BASES + 1)];
    u8 random_bytes[144];
    u32 seed = 0x9e3779b9u, i, j;

    CHECK(analyze("والكتاب", &r));
    CHECK(has_chain(&r, article_chain, 3));
    CHECK(analyze("سيكتب", &r));
    CHECK(has_chain(&r, verb_chain, 3));
    {
        u32 k; int found = 0;
        for (k = 0; k < r.count; ++k)
            if ((r.analyses[k].flags & MF_VERB_CANDIDATE) &&
                r.analyses[k].verb_id == 1) found = 1;
        CHECK(found);
    }
    CHECK(analyze("كاتب", &r));
    CHECK(has_pattern_root(&r, MP_FAAIL, 0x643, 0x62A, 0x628));
    CHECK(analyze("كتاب", &r));
    CHECK(has_pattern_root(&r, MP_FIAAL, 0x643, 0x62A, 0x628));
    CHECK(analyze("مكتوب", &r));
    CHECK(has_pattern_root(&r, MP_MAFOOL, 0x643, 0x62A, 0x628));
    CHECK(analyze("Al-book", &r));
    CHECK(has_chain(&r, ascii_chain, 2));
    CHECK(analyze("بكِتابه", &r));  /* diacritics remain byte-exact */
    CHECK(analyze("سوف", &r));
    CHECK(analyze("ABC", &r));
    CHECK(r.count == 1);
    CHECK(analyze("", &r));
    CHECK(morph_analyze(invalid, sizeof invalid, &r) == MORPH_INVALID_UTF8);
    CHECK(r.count == 0);
    CHECK(morph_analyze(overlong, sizeof overlong, &r) == MORPH_INVALID_UTF8);
    CHECK(r.count == 0);
    CHECK(morph_analyze(invalid_cont, sizeof invalid_cont, &r) == MORPH_INVALID_UTF8);
    CHECK(r.count == 0);
    CHECK(morph_analyze((const u8 *)"test", 4, NULL) == MORPH_BAD_ARGUMENT);
    CHECK(morph_analyze(NULL, 1, &r) == MORPH_BAD_ARGUMENT);
    CHECK(r.count == 0);
    for (i = 0; i < sizeof long_word / 2; i++) {
        long_word[2*i] = 0xD9; long_word[2*i + 1] = 0x83; /* ك */
    }
    CHECK(morph_analyze(long_word, sizeof long_word, &r) == MORPH_TOO_LONG);
    CHECK(r.truncated == 1 && r.count == 1 && valid_parts(long_word, sizeof long_word, &r));

    /* Fixed-seed byte corpus: invalid UTF-8 must fail without a segmentation result. */
    for (i = 0; i < 5000; ++i) {
        u32 n = i % (u32)sizeof random_bytes;
        MorphStatus status;
        for (j = 0; j < n; ++j) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            random_bytes[j] = (u8)seed;
        }
        status = morph_analyze(random_bytes, n, &r);
        CHECK(status == MORPH_OK || status == MORPH_INVALID_UTF8 ||
              status == MORPH_TOO_LONG);
        if (status == MORPH_INVALID_UTF8) CHECK(r.count == 0);
        else CHECK(r.count >= 1 && valid_parts(random_bytes, n, &r));
    }
    puts("morph_test: 5000 byte-corpus cases and deterministic Arabic fixtures PASS");
    return 0;
}
