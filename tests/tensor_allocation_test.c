#define _GNU_SOURCE
#include "tensor_core.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Hosted verification only. Link with -Wl,--wrap=tensor_allocate. */
enum { MAX_VALUES = 64, WIRE_BYTES = 528, RESULT_BYTES = 72 };
typedef struct { u32 begin, end; } Range;
static unsigned direct_cases, consumer_cases, abi_cases, wrapped_calls, mutations;
static AstNode arena[4096];
static u8 exported[131072];

static void die(const char *label, const char *condition, int line) {
    fprintf(stderr, "ALLOCATION_TEST_FAIL line=%d case=%s condition=%s "
            "direct=%u consumer=%u abi=%u\n",
            line, label, condition, direct_cases, consumer_cases, abi_cases);
    exit(1);
}
#define CHECK(c, label) do { if (!(c)) die((label), #c, __LINE__); } while (0)

extern int __real_tensor_allocate(const u8 *, u32, u8 *, u32);

static void put32(u8 *p, u32 x) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (u8)(x >> (8 * i));
}
static u32 wire(u8 *out, const Range *r, u32 n, u32 mask) {
    CHECK(n >= 1 && n <= MAX_VALUES && mask <= 255, "wire fixture bounds");
    memset(out, 0, WIRE_BYTES);
    memcpy(out, "NTILE001", 8);
    out[8] = (u8)n;
    out[9] = (u8)mask;
    for (u32 i = 0; i < n; ++i) {
        put32(out + 16 + 8 * i, r[i].begin);
        put32(out + 20 + 8 * i, r[i].end);
    }
    return 16 + 8 * n;
}
static unsigned available(u32 mask) {
    unsigned n = 0;
    for (unsigned bit = 0; bit < 8; ++bit) n += !(mask & (1u << bit));
    return n;
}
static unsigned peak(const Range *r, u32 n) {
    unsigned result = 0;
    for (u32 i = 0; i < n; ++i) {
        unsigned live = 0;
        for (u32 j = 0; j < n; ++j)
            live += r[j].begin <= r[i].begin && r[i].begin < r[j].end;
        if (live > result) result = live;
    }
    return result;
}
static void assignment(const char *label, const u8 *out, const Range *r,
                       u32 n, u32 mask, const u8 *exact_colors) {
    CHECK(!memcmp(out, "TMM1", 4) && out[4] == n, label);
    CHECK(out[5] == 0 && out[6] == 0 && out[7] == 0, label);
    unsigned used = 0, distinct = 0;
    for (u32 i = 0; i < n; ++i) {
        unsigned color = out[8 + i];
        CHECK(color < 8, label);
        CHECK(!(mask & (1u << color)), label);
        used |= 1u << color;
        for (u32 j = 0; j < i; ++j)
            if (r[i].begin < r[j].end && r[j].begin < r[i].end)
                CHECK(color != out[8 + j], label);
    }
    for (unsigned bit = 0; bit < 8; ++bit) distinct += !!(used & (1u << bit));
    CHECK(distinct == peak(r, n), label);
    if (exact_colors) CHECK(!memcmp(out + 8, exact_colors, n), label);
}
static void raw_case(const char *label, const u8 *input, u32 bytes, u32 capacity,
                     int expected, const Range *r, u32 n, u32 mask,
                     int null_output, const u8 *exact_colors) {
    u8 guarded[16 + RESULT_BYTES + 16];
    memset(guarded, 0xA5, sizeof guarded);
    u8 *out = guarded + 16;
    int status = __real_tensor_allocate(input, bytes,
                                       null_output ? NULL : out, capacity);
    CHECK(status == expected, label);
    if (!status) {
        CHECK(!null_output && r && n >= 1 && n <= MAX_VALUES, label);
        assignment(label, out, r, n, mask, exact_colors);
    }
    for (u32 i = 0; i < sizeof guarded; ++i)
        if (status || i < 16 || i >= 16 + 8 + n)
            CHECK(guarded[i] == 0xA5, label);
    ++direct_cases;
}
static void valid_case(const char *label, const Range *r, u32 n, u32 mask,
                       const u8 *exact_colors) {
    u8 input[WIRE_BYTES];
    u32 bytes = wire(input, r, n, mask);
    int expected = peak(r, n) <= available(mask) ? 0 : 77;
    raw_case(label, input, bytes, RESULT_BYTES, expected,
             r, n, mask, 0, exact_colors);
}
static void rejection(const char *label, const u8 *input, u32 bytes,
                      u32 capacity, int expected) {
    raw_case(label, input, bytes, capacity, expected, NULL, 0, 0, 0, NULL);
}
static void direct_contract(void) {
    Range r[MAX_VALUES];
    char label[64];
    for (u32 mask = 0; mask < 256; ++mask) {
        u32 capacity = available(mask);
        /* N=0 is invalid: mask 255 has no nonempty fit case. */
        u32 n = capacity ? capacity : 1;
        for (u32 i = 0; i < n; ++i) r[i] = (Range){0, 5};
        (void)snprintf(label, sizeof label, "mask_%u_fit_nonempty", mask);
        valid_case(label, r, n, mask, NULL);
        n = capacity + 1;
        for (u32 i = 0; i < n; ++i) r[i] = (Range){0, 5};
        (void)snprintf(label, sizeof label, "mask_%u_pressure", mask);
        valid_case(label, r, n, mask, NULL);
        for (u32 i = 0; i < MAX_VALUES; ++i) r[i] = (Range){i, i + 1};
        (void)snprintf(label, sizeof label, "mask_%u_endpoint_reuse", mask);
        valid_case(label, r, MAX_VALUES, mask, NULL);
    }
    const Range gemm[] = {{0, 3}, {1, 3}, {2, 4}};
    const u8 gemm_colors[] = {0, 1, 2};
    valid_case("gemm_operands_and_result_overlap", gemm, 3, 0, gemm_colors);
    const Range ties[] = {{0, 2}, {0, 1}, {1, 2}, {2, 3}};
    const u8 tie_colors[] = {0, 1, 1, 0};
    valid_case("equal_start_input_order_lowest_color", ties, 4, 0, tie_colors);
    const Range unused[] = {{0, 4}, {1, 4}, {2, 3}, {3, 5}};
    const u8 unused_colors[] = {0, 1, 2, 2};
    valid_case("unused_single_instruction_then_reuse", unused, 4, 0, unused_colors);
    const Range extremes[] = {
        {0x7ffffffeu, 0x80000000u},
        {0x80000000u, 0x80000001u},
        {0xfffffffeu, 0xffffffffu}
    };
    const u8 reused[] = {0, 0, 0};
    valid_case("unsigned_endpoint_boundaries", extremes, 3, 0, reused);
    const Range high[] = {{0xfffffffeu, 0xffffffffu}};
    const Range low[] = {{0, 1}};
    valid_case("state_reset_high_call", high, 1, 0, NULL);
    valid_case("state_reset_low_call", low, 1, 0, NULL);

    u8 good[WIRE_BYTES + 1] = {0}, bad[WIRE_BYTES + 1];
    u32 bytes = wire(good, gemm, 3, 1);
    good[bytes] = 0;
    for (u32 length = 0; length < bytes; ++length) {
        (void)snprintf(label, sizeof label, "truncated_%u", length);
        rejection(label, good, length, RESULT_BYTES, 65);
    }
    rejection("trailing_zero_byte", good, bytes + 1, RESULT_BYTES, 65);
    memcpy(bad, good, sizeof bad); bad[0] ^= 1;
    rejection("invalid_magic", bad, bytes, RESULT_BYTES, 65);
    const unsigned invalid_n[] = {0, 65, 255};
    for (unsigned i = 0; i < sizeof invalid_n / sizeof *invalid_n; ++i) {
        memcpy(bad, good, sizeof bad); bad[8] = (u8)invalid_n[i];
        rejection("invalid_value_count", bad, bytes, RESULT_BYTES, 65);
    }
    for (unsigned i = 10; i < 16; ++i) {
        memcpy(bad, good, sizeof bad); bad[i] = 1;
        rejection("nonzero_reserved_header", bad, bytes, RESULT_BYTES, 65);
    }
    const Range equal[] = {{2, 2}};
    const Range reverse[] = {{2, 1}};
    const Range descending[] = {{5, 9}, {4, 6}};
    const Range wrapped[] = {{0xffffffffu, 0}};
    const Range max_equal[] = {{0xffffffffu, 0xffffffffu}};
    const Range invalid_after_pressure[] = {{0, 3}, {0, 3}, {0, 3}, {4, 4}};
    u32 len = wire(bad, equal, 1, 0);
    rejection("empty_interval", bad, len, RESULT_BYTES, 65);
    len = wire(bad, reverse, 1, 0);
    rejection("reversed_interval", bad, len, RESULT_BYTES, 65);
    len = wire(bad, descending, 2, 0);
    rejection("unsorted_starts", bad, len, RESULT_BYTES, 65);
    len = wire(bad, wrapped, 1, 0);
    rejection("wrapped_endpoint", bad, len, RESULT_BYTES, 65);
    len = wire(bad, max_equal, 1, 0);
    rejection("equal_uint32_max", bad, len, RESULT_BYTES, 65);
    len = wire(bad, invalid_after_pressure, 4, 0xFC);
    rejection("validate_all_before_pressure", bad, len, RESULT_BYTES, 65);
    len = wire(bad, equal, 1, 255);
    rejection("invalid_before_zero_capacity", bad, len, RESULT_BYTES, 65);
    for (u32 cap = 0; cap < 11; ++cap)
        rejection("short_destination_capacity", good, bytes, cap, 65);
    rejection("uint32_max_input_bytes", good, UINT32_MAX, RESULT_BYTES, 65);
    rejection("uint32_max_minus_one_input_bytes", good, UINT32_MAX - 1, RESULT_BYTES, 65);
    rejection("null_input", NULL, bytes, RESULT_BYTES, 65);
    raw_case("null_destination", good, bytes, RESULT_BYTES, 65,
             NULL, 0, 0, 1, NULL);
    raw_case("large_capacity_bounded_write", good, bytes, UINT32_MAX, 0,
             gemm, 3, 1, 0, NULL);
}
static void guarded_boundaries(void) {
    const Range ranges[] = {{0, 3}, {1, 3}, {2, 4}};
    u8 input[WIRE_BYTES];
    u32 bytes = wire(input, ranges, 3, 0);
    long page_long = sysconf(_SC_PAGESIZE);
    CHECK(page_long >= WIRE_BYTES, "page size");
    size_t page = (size_t)page_long;
    u8 *mapping = mmap(NULL, 3 * page, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(mapping != MAP_FAILED, "guard mapping");
    for (u32 length = 0; length <= bytes; ++length) {
        CHECK(mprotect(mapping + page, page, PROT_READ | PROT_WRITE) == 0,
              "input writable page");
        u8 *at = mapping + 2 * page - length;
        if (length) memcpy(at, input, length);
        CHECK(mprotect(mapping + page, page, PROT_READ) == 0,
              "input read-only page");
        raw_case("input_ends_at_guard_page", at, length, RESULT_BYTES,
                 length == bytes ? 0 : 65, ranges, 3, 0, 0, NULL);
    }
    CHECK(mprotect(mapping + page, page, PROT_READ | PROT_WRITE) == 0,
          "output writable page");
    u8 *out = mapping + 2 * page - 11;
    memset(out, 0xA5, 11);
    CHECK(__real_tensor_allocate(input, bytes, out, 11) == 0, "output guard success");
    assignment("output guard assignment", out, ranges, 3, 0, NULL);
    ++direct_cases;
    for (u32 cap = 0; cap < 11; ++cap) {
        out = mapping + 2 * page - cap;
        if (cap) memset(out, 0xA5, cap);
        CHECK(__real_tensor_allocate(input, bytes, out, cap) == 65,
              "short output at guard page");
        for (u32 i = 0; i < cap; ++i) CHECK(out[i] == 0xA5, "short output unchanged");
        ++direct_cases;
    }
    CHECK(munmap(mapping, 3 * page) == 0, "guard unmap");
}

#if !defined(__x86_64__)
#error This ABI probe requires the specified Linux x86-64 target.
#endif
extern int tensor_allocator_abi_probe(const u8 *, u32, u8 *, u32, int *);
/* Six pushes + 40 bytes align RSP to 16 before CALL. No red-zone storage.
 * The probe restores its own caller's registers even on a detected mismatch. */
__asm__(
".text\n"
".p2align 4\n"
".globl tensor_allocator_abi_probe\n"
".type tensor_allocator_abi_probe,@function\n"
"tensor_allocator_abi_probe:\n"
"push %rbp\n"
"push %rbx\n"
"push %r12\n"
"push %r13\n"
"push %r14\n"
"push %r15\n"
"sub $40,%rsp\n"
"mov %r8,0(%rsp)\n"
"mov %rsp,8(%rsp)\n"
"movabs $0x13579bdf2468ace0,%r10\n"
"mov %r10,16(%rsp)\n"
"movabs $0x1122334455667788,%rbx\n"
"movabs $0x22446688aaccee01,%rbp\n"
"movabs $0x3344556677889902,%r12\n"
"movabs $0x445566778899aa03,%r13\n"
"movabs $0x5566778899aabb04,%r14\n"
"movabs $0x66778899aabbcc05,%r15\n"
"cld\n"
"call __real_tensor_allocate\n"
"mov 0(%rsp),%r10\n"
"mov %eax,(%r10)\n"
"cmp %rsp,8(%rsp)\n"
"jne .Lallocation_abi_bad\n"
"movabs $0x13579bdf2468ace0,%r10\n"
"cmp %r10,16(%rsp)\n"
"jne .Lallocation_abi_bad\n"
"movabs $0x1122334455667788,%r10\n"
"cmp %r10,%rbx\n"
"jne .Lallocation_abi_bad\n"
"movabs $0x22446688aaccee01,%r10\n"
"cmp %r10,%rbp\n"
"jne .Lallocation_abi_bad\n"
"movabs $0x3344556677889902,%r10\n"
"cmp %r10,%r12\n"
"jne .Lallocation_abi_bad\n"
"movabs $0x445566778899aa03,%r10\n"
"cmp %r10,%r13\n"
"jne .Lallocation_abi_bad\n"
"movabs $0x5566778899aabb04,%r10\n"
"cmp %r10,%r14\n"
"jne .Lallocation_abi_bad\n"
"movabs $0x66778899aabbcc05,%r10\n"
"cmp %r10,%r15\n"
"jne .Lallocation_abi_bad\n"
"pushfq\n"
"pop %r10\n"
"test $0x400,%r10\n"
"jnz .Lallocation_abi_bad\n"
"xor %eax,%eax\n"
"jmp .Lallocation_abi_done\n"
".Lallocation_abi_bad:\n"
"mov $1,%eax\n"
".Lallocation_abi_done:\n"
"cld\n"
"add $40,%rsp\n"
"pop %r15\n"
"pop %r14\n"
"pop %r13\n"
"pop %r12\n"
"pop %rbx\n"
"pop %rbp\n"
"ret\n"
".size tensor_allocator_abi_probe,.-tensor_allocator_abi_probe\n"
);
static void abi_contract(void) {
    u8 input[WIRE_BYTES], output[RESULT_BYTES];
    Range one[] = {{0, 1}};
    const int expected[] = {0, 77, 65};
    for (unsigned i = 0; i < 3; ++i) {
        one[0].end = i == 2 ? 0 : 1;
        u32 bytes = wire(input, one, 1, i == 1 ? 255 : 0);
        memset(output, 0xA5, sizeof output);
        int status = -1;
        CHECK(tensor_allocator_abi_probe(input, bytes, output, sizeof output, &status) == 0,
              "callee-saved registers stack and DF");
        CHECK(status == expected[i], "ABI return status");
        if (!status) assignment("ABI successful result", output, one, 1, 0, NULL);
        for (u32 j = status ? 0 : 9; j < sizeof output; ++j)
            CHECK(output[j] == 0xA5, "ABI output extent");
        ++abi_cases;
    }
}

enum Fault {
    FAULT_NONE, FAULT_MAGIC, FAULT_COUNT, FAULT_RESERVED_5, FAULT_RESERVED_6,
    FAULT_RESERVED_7, FAULT_COLOR_RANGE, FAULT_RESERVED_COLOR, FAULT_CONFLICT
};
static enum Fault fault;
int __wrap_tensor_allocate(const u8 *input, u32 bytes, u8 *output, u32 capacity) {
    ++wrapped_calls;
    int status = __real_tensor_allocate(input, bytes, output, capacity);
    if (status || fault == FAULT_NONE) return status;
    CHECK(input[8] >= 3 && (input[9] & 1), "fault fixture preconditions");
    switch (fault) {
        case FAULT_MAGIC: output[0] ^= 1; break;
        case FAULT_COUNT: ++output[4]; break;
        case FAULT_RESERVED_5: output[5] = 1; break;
        case FAULT_RESERVED_6: output[6] = 1; break;
        case FAULT_RESERVED_7: output[7] = 1; break;
        case FAULT_COLOR_RANGE: output[8] = 8; break;
        case FAULT_RESERVED_COLOR: output[8] = 0; break;
        case FAULT_CONFLICT: output[8 + input[8] - 1] = output[8]; break;
        case FAULT_NONE: break;
    }
    ++mutations;
    return status;
}
static const u8 consumer_source[] =
    "reserve 1; input_a a; input_b b; input_a unused; "
    "gemm c = a * b; emit c;";
static u32 parse(Parser *p, const u8 *source, size_t bytes) {
    parser_init(p, arena, 4096, source, bytes);
    u32 root = parser_parse(p);
    CHECK(root && !p->error && p->has_tensor, "consumer fixture parse");
    return root;
}
static void no_exports(void) {
    memset(exported, 0xA5, sizeof exported);
    CHECK(tensor_export_ir(exported, sizeof exported) == 0, "rejected IR export");
    for (u32 i = 0; i < sizeof exported; ++i)
        CHECK(exported[i] == 0xA5, "rejected IR destination unchanged");
    CHECK(tensor_export_asm(exported, sizeof exported) == 0, "rejected ASM export");
    for (u32 i = 0; i < sizeof exported; ++i)
        CHECK(exported[i] == 0xA5, "rejected ASM destination unchanged");
}
static void consumer_success(void) {
    Parser p; CodegenResult result;
    u32 root = parse(&p, consumer_source, sizeof consumer_source - 1);
    fault = FAULT_NONE;
    unsigned calls = wrapped_calls;
    CHECK(tensor_prepare(&p, root, &result) && result.exit_code == 0,
          "valid consumer prepare");
    CHECK(wrapped_calls == calls + 1, "consumer calls allocator once");
    const int starts[] = {0, 1, 2, 3}, ends[] = {4, 4, 3, 5};
    int colors[4] = {-1, -1, -1, -1};
    unsigned definitions = 0;
    for (u32 i = 1; i < p.count; ++i) {
        Node_TensorOp *n = &arena[i].tensor;
        if (arena[i].kind != A_TENSOR_OP ||
            (n->opcode != TI_INPUT_A && n->opcode != TI_INPUT_B && n->opcode != TI_GEMM))
            continue;
        CHECK(n->metadata >= 1 && n->metadata <= 4, "definition metadata index");
        unsigned v = n->metadata - 1;
        CHECK(n->lifespan_start == starts[v] && n->lifespan_end == ends[v],
              "reserve excluded and unused lifespan");
        CHECK(n->tmm > 0 && n->tmm < 8, "consumer legal unreserved color");
        CHECK(colors[v] == -1, "unique definition value");
        colors[v] = n->tmm;
        ++definitions;
    }
    CHECK(definitions == 4, "consumer definition count");
    CHECK(colors[0] != colors[1] && colors[0] != colors[3] && colors[1] != colors[3],
          "GEMM result cannot overwrite operands");
    CHECK(colors[2] == colors[3], "unused color reusable at touching endpoint");
    CHECK(tensor_export_ir(exported, sizeof exported) > 0, "valid IR export");
    CHECK(tensor_export_asm(exported, sizeof exported) > 0, "valid ASM export");
    ++consumer_cases;
}
static void consumer_contract(void) {
    consumer_success();
    for (int kind = FAULT_MAGIC; kind <= FAULT_CONFLICT; ++kind) {
        Parser p; CodegenResult result;
        u32 root = parse(&p, consumer_source, sizeof consumer_source - 1);
        fault = (enum Fault)kind;
        unsigned calls = wrapped_calls, changed = mutations;
        CHECK(!tensor_prepare(&p, root, &result) && result.exit_code == 65 && result.size == 0,
              "consumer rejects corrupt allocation");
        CHECK(wrapped_calls == calls + 1 && mutations == changed + 1,
              "corruption actually applied");
        no_exports();
        ++consumer_cases;
        consumer_success();
    }
    static const u8 pressure_source[] =
        "input_a a0; input_a a1; input_a a2; input_a a3; "
        "input_a a4; input_a a5; input_a a6; input_a a7; input_b b; "
        "gemm c0 = a0 * b; emit c0; gemm c1 = a1 * b; emit c1; "
        "gemm c2 = a2 * b; emit c2; gemm c3 = a3 * b; emit c3; "
        "gemm c4 = a4 * b; emit c4; gemm c5 = a5 * b; emit c5; "
        "gemm c6 = a6 * b; emit c6; gemm c7 = a7 * b; emit c7;";
    Parser p; CodegenResult result;
    u32 root = parse(&p, pressure_source, sizeof pressure_source - 1);
    fault = FAULT_NONE;
    unsigned calls = wrapped_calls;
    CHECK(!tensor_prepare(&p, root, &result) && result.exit_code == 77 && result.size == 0,
          "consumer preserves no-spill pressure rejection");
    CHECK(wrapped_calls == calls + 1, "pressure reaches callable allocator");
    no_exports();
    ++consumer_cases;
}
int main(void) {
    direct_contract();
    guarded_boundaries();
    abi_contract();
    consumer_contract();
    printf("TENSOR_ALLOCATION_PASS direct=%u consumer=%u abi=%u "
           "wrapper_calls=%u mutations=%u cases=%u\n",
           direct_cases, consumer_cases, abi_cases, wrapped_calls, mutations,
           direct_cases + consumer_cases + abi_cases);
    return 0;
}
