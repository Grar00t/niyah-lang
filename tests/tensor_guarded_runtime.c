#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#if !defined(__x86_64__) || !defined(__GNUC__)
#error "This native runtime test requires GCC-compatible hosted x86-64."
#endif
_Static_assert(sizeof(int32_t) == 4 && CHAR_BIT == 8, "tensor storage contract");
enum { TILE_BYTES = 1024, ELEMENTS = 256, FIXED_CASES = 12, RANDOM_CASES = 32 };

extern void guarded_scalar(const int8_t *, const int8_t *, int32_t *);
extern void guarded_avx2(const int8_t *, const int8_t *, int32_t *);
extern void guarded_avx512(const int8_t *, const int8_t *, int32_t *);
extern void guarded_pack(const int8_t *, uint8_t *);
typedef void (*Gemm)(const int8_t *, const int8_t *, int32_t *);
typedef struct {
    uint8_t *mapping, *page, *data;
    size_t page_size, offset;
    unsigned id;
} Guard;
static unsigned fixture_now, placement_now;
static const char *mode_now = "setup";
static uint64_t guard_checks, input_checks, elements_checked;

static void fail(const char *why) {
    fprintf(stderr, "guarded runtime: fixture=%u placement=%u mode=%s: %s\n",
            fixture_now, placement_now, mode_now, why);
    exit(1);
}
static uint8_t canary(size_t i, unsigned id) {
    return (uint8_t)((i * 131u + id * 17u + 0x5au) & 255u);
}
static void create_guard(Guard *g, size_t page_size, unsigned id) {
    g->page_size = page_size;
    g->id = id;
    g->mapping = mmap(NULL, page_size * 3, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g->mapping == MAP_FAILED) fail("mmap failed");
    g->page = g->mapping + page_size;
}
static void place_guard(Guard *g, int at_end) {
    if (mprotect(g->page, g->page_size, PROT_READ | PROT_WRITE))
        fail("mprotect writable middle page failed");
    g->offset = at_end ? g->page_size - TILE_BYTES : 0;
    g->data = g->page + g->offset;
    for (size_t i = 0; i < g->page_size; ++i) g->page[i] = canary(i, g->id);
    memset(g->data, 0xa5, TILE_BYTES);
}
static void check_guard(const Guard *g) {
    for (size_t i = 0; i < g->page_size; ++i)
        if ((i < g->offset || i >= g->offset + TILE_BYTES) &&
            g->page[i] != canary(i, g->id))
            fail("surrounding canary changed");
    ++guard_checks;
}
static void check_all(const Guard g[4], const int8_t a[TILE_BYTES],
                      const int8_t b[TILE_BYTES], const uint8_t packed[TILE_BYTES]) {
    for (unsigned i = 0; i < 4; ++i) check_guard(&g[i]);
    if (memcmp(g[0].data, a, TILE_BYTES) || memcmp(g[1].data, b, TILE_BYTES))
        fail("input bytes changed");
    input_checks += 2;
    if (memcmp(g[2].data, packed, TILE_BYTES)) fail("packed B mismatch");
}
static int8_t signed_byte(uint32_t x) {
    int value = (int)(x & 255u);
    return (int8_t)(value < 128 ? value : value - 256);
}
static uint32_t random_u32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return x;
}
static void fill_fixture(unsigned which, int8_t a[TILE_BYTES],
                         int8_t b[TILE_BYTES], uint32_t *rng) {
    memset(a, 0, TILE_BYTES); memset(b, 0, TILE_BYTES);
    if (which <= 4) {
        static const int av[] = {0, 1, -128, -128, 127};
        static const int bv[] = {127, 2, -128, 127, 127};
        for (unsigned i = 0; i < TILE_BYTES; ++i) {
            a[i] = (int8_t)av[which]; b[i] = (int8_t)bv[which];
        }
    } else if (which == 5) {
        for (unsigned i = 0; i < TILE_BYTES; ++i) {
            a[i] = (int8_t)((i & 1u) ? -128 : 127);
            b[i] = (int8_t)((i % 3u) ? 127 : -128);
        }
    } else if (which == 6) {
        for (unsigned i = 0; i < TILE_BYTES; ++i) {
            a[i] = signed_byte((i / 64u) * 47u + (i % 64u) * 7u + 19u);
            b[i] = signed_byte((i / 16u) * 13u + (i % 16u) * 29u + 61u);
        }
    } else if (which < FIXED_CASES) {
        static const unsigned impulse_k[] = {0, 1, 3, 4, 63};
        unsigned k = impulse_k[which - 7], row = k % 16;
        a[row * 64 + k] = 1;
        for (unsigned j = 0; j < 16; ++j) b[k * 16 + j] = (int8_t)((int)j - 8);
    } else {
        for (unsigned i = 0; i < TILE_BYTES; ++i) {
            a[i] = signed_byte(random_u32(rng));
            b[i] = signed_byte(random_u32(rng));
        }
    }
}
static void reference(const int8_t a[TILE_BYTES], const int8_t b[TILE_BYTES],
                      int32_t c[ELEMENTS], uint8_t packed[TILE_BYTES]) {
    for (unsigned i = 0; i < 16; ++i)
        for (unsigned j = 0; j < 16; ++j) {
            int64_t sum = 0;
            for (unsigned k = 0; k < 64; ++k)
                sum += (int64_t)a[i * 64 + k] * (int64_t)b[k * 16 + j];
            if (sum < INT32_MIN || sum > INT32_MAX) fail("reference overflow");
            c[i * 16 + j] = (int32_t)sum;
        }
    for (unsigned group = 0; group < 16; ++group)
        for (unsigned j = 0; j < 16; ++j)
            for (unsigned lane = 0; lane < 4; ++lane)
                packed[group * 64 + 4 * j + lane] =
                    (uint8_t)b[(4 * group + lane) * 16 + j];
}
int main(void) {
    long page_query = sysconf(_SC_PAGESIZE);
    if (page_query < TILE_BYTES || (uint64_t)page_query > SIZE_MAX / 3)
        fail("unsupported page size");
    size_t page_size = (size_t)page_query;
    Guard g[4] = {{0}};
    for (unsigned i = 0; i < 4; ++i) create_guard(&g[i], page_size, i);
    __builtin_cpu_init();
    int avx2 = __builtin_cpu_supports("avx2") != 0;
    int avx512 = __builtin_cpu_supports("avx512f") != 0;
    const char *names[] = {"scalar", "avx2", "avx512"};
    Gemm kernels[] = {guarded_scalar, guarded_avx2, guarded_avx512};
    int enabled[] = {1, avx2, avx512};
    uint64_t calls[3] = {0, 0, 0}, pack_calls = 0;
    uint32_t rng = UINT32_C(0x4e495941);
    unsigned completed = 0;
    int8_t a[TILE_BYTES], b[TILE_BYTES];
    uint8_t packed[TILE_BYTES];
    int32_t expected[ELEMENTS];
    for (fixture_now = 0; fixture_now < FIXED_CASES + RANDOM_CASES; ++fixture_now) {
        mode_now = "reference";
        fill_fixture(fixture_now, a, b, &rng);
        reference(a, b, expected, packed);
        if (fixture_now <= 4) {
            static const int32_t constant[] = {0, 128, 1048576, -1040384, 1032256};
            for (unsigned i = 0; i < ELEMENTS; ++i)
                if (expected[i] != constant[fixture_now]) fail("constant oracle mismatch");
        }
        for (placement_now = 0; placement_now < 16; ++placement_now) {
            for (unsigned i = 0; i < 4; ++i)
                place_guard(&g[i], (int)((placement_now >> i) & 1u));
            memcpy(g[0].data, a, TILE_BYTES); memcpy(g[1].data, b, TILE_BYTES);
            if (mprotect(g[0].page, page_size, PROT_READ) ||
                mprotect(g[1].page, page_size, PROT_READ))
                fail("mprotect read-only inputs failed");
            mode_now = "pack";
            guarded_pack((const int8_t *)g[1].data, g[2].data);
            check_all(g, a, b, packed); ++pack_calls;
            for (unsigned mode = 0; mode < 3; ++mode) {
                if (!enabled[mode]) continue;
                mode_now = names[mode];
                place_guard(&g[3], (int)((placement_now >> 3) & 1u));
                kernels[mode]((const int8_t *)g[0].data,
                              (const int8_t *)g[1].data, (int32_t *)g[3].data);
                const int32_t *actual = (const int32_t *)g[3].data;
                for (unsigned i = 0; i < ELEMENTS; ++i) {
                    if (actual[i] != expected[i]) fail("INT32 result differs from int64 C oracle");
                    ++elements_checked;
                }
                check_all(g, a, b, packed); ++calls[mode];
            }
        }
        ++completed;
    }
    mode_now = "cleanup";
    for (unsigned i = 0; i < 4; ++i)
        if (munmap(g[i].mapping, page_size * 3)) fail("munmap failed");
    printf("{\"status\":\"PASS\",\"fixtures\":%u,\"placements_per_fixture\":16,", completed);
    printf("\"page_bytes\":%zu,\"random_seed\":%" PRIu32 ",", page_size, UINT32_C(0x4e495941));
    printf("\"gemm_calls\":{\"scalar\":%" PRIu64 ",\"avx2\":%" PRIu64
           ",\"avx512\":%" PRIu64 "},", calls[0], calls[1], calls[2]);
    printf("\"packed_B_calls\":%" PRIu64 ",\"packed_B_bytes_checked\":%" PRIu64 ",",
           pack_calls, pack_calls * TILE_BYTES);
    printf("\"INT32_elements_compared\":%" PRIu64 ",\"guard_canary_checks\":%" PRIu64
           ",\"input_payload_checks\":%" PRIu64 ",",
           elements_checked, guard_checks, input_checks);
    printf("\"available_avx2\":%s,\"available_avx512\":%s,",
           avx2 ? "true" : "false", avx512 ? "true" : "false");
    printf("\"amx_hardware_executed\":false,\"avx512_hardware_executed\":%s}\n",
           calls[2] ? "true" : "false");
    return 0;
}
