#include "eval.h"
#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SOURCE_MAX = 1 << 20, NODES_MAX = 4096 };
static u8 source[SOURCE_MAX + 1];
static AstNode nodes[NODES_MAX];

int main(int argc, char **argv) {
    FILE *f;
    size_t n;
    Parser parser;
    Checker checker;
    EvalResult result;
    u32 root;
    int read_failed, close_failed;
    if (argc != 2) { fputs("usage: niyahrun FILE\n", stderr); return 1; }
    f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    n = fread(source, 1, SOURCE_MAX + 1, f);
    read_failed = ferror(f);
    close_failed = fclose(f);
    if (read_failed || close_failed || n > SOURCE_MAX) {
        fputs("error: read failed or input too large\n", stderr);
        return 1;
    }
    parser_init(&parser, nodes, NODES_MAX, source, (u64)n);
    root = parser_parse(&parser);
    if (!root) {
        fprintf(stderr, "syntax: %u:%u %s\n", parser.error_at.line,
                parser.error_at.col, parser.error ? parser.error : "unknown");
        return 2;
    }
    checker_init(&checker, &parser);
    if (!checker_run(&checker, root)) {
        fprintf(stderr, "semantic: %u:%u %s\n", checker.at.line,
                checker.at.col, checker.message ? checker.message : "unknown");
        return 5;
    }
    if (!eval_main(&parser, root, &result)) {
        fprintf(stderr, "evaluation: %u:%u %s\n", result.at.line,
                result.at.col, result.error ? result.error : "invalid program");
        return 3;
    }
    if (printf("RESULT=%ld\n", (long)result.value) < 0 || fflush(stdout) == EOF) {
        fputs("error: failed to write result\n", stderr);
        return 4;
    }
    return 0;
}
