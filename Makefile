CC ?= cc
HOST_CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
FREE_CFLAGS = $(HOST_CFLAGS) -ffreestanding -fno-stack-protector -fno-pic -no-pie
FREE_LDFLAGS = -static -nostdlib
FRONTEND = src/check.c src/parser.c src/lexer.c
HEADERS = src/check.h src/parser.h src/lexer.h

all: niyahlex niyahparse niyahrun niyahc

niyahlex: src/lexer.c src/lexer.h src/morph.c src/morph.h
	$(CC) $(FREE_CFLAGS) -DNIYAH_LEXER_STANDALONE $(FREE_LDFLAGS) -o $@ src/lexer.c src/morph.c

niyahparse: src/parser_cli.c $(FRONTEND) $(HEADERS)
	$(CC) $(FREE_CFLAGS) $(FREE_LDFLAGS) -o $@ src/parser_cli.c $(FRONTEND)

niyahrun: src/run_cli.c src/eval.c src/eval.h $(FRONTEND) $(HEADERS)
	$(CC) $(HOST_CFLAGS) -o $@ src/run_cli.c src/eval.c $(FRONTEND)

niyahc: src/compile_cli.c src/codegen.c src/codegen.h $(FRONTEND) $(HEADERS)
	$(CC) $(HOST_CFLAGS) -o $@ src/compile_cli.c src/codegen.c $(FRONTEND)

tests/lexer_test: tests/lexer_test.c src/lexer.c src/lexer.h
	$(CC) $(HOST_CFLAGS) -o $@ tests/lexer_test.c src/lexer.c

tests/lexer_recovery_test: tests/lexer_recovery_test.c src/lexer.c src/lexer.h
	$(CC) $(HOST_CFLAGS) -o $@ tests/lexer_recovery_test.c src/lexer.c

tests/morph_test: tests/morph_test.c src/morph.c src/morph.h src/lexer.h
	$(CC) $(HOST_CFLAGS) -o $@ tests/morph_test.c src/morph.c

tests/parser_test: tests/parser_test.c src/parser.c src/parser.h src/lexer.c src/lexer.h
	$(CC) $(HOST_CFLAGS) -o $@ tests/parser_test.c src/parser.c src/lexer.c

tests/check_test: tests/check_test.c $(FRONTEND) $(HEADERS)
	$(CC) $(HOST_CFLAGS) -o $@ tests/check_test.c $(FRONTEND)

tests/eval_test: tests/eval_test.c src/eval.c src/eval.h $(FRONTEND) $(HEADERS)
	$(CC) $(HOST_CFLAGS) -o $@ tests/eval_test.c src/eval.c $(FRONTEND)

test: all tests/lexer_test tests/lexer_recovery_test tests/morph_test tests/parser_test tests/check_test tests/eval_test
	./niyahlex examples/مرحبا.نيّة > tests/lex/مرحبا.actual
	cmp tests/lex/مرحبا.expected tests/lex/مرحبا.actual
	./tests/lexer_test
	./tests/lexer_recovery_test
	./tests/morph_test
	./tests/parser_test
	./tests/check_test
	./tests/eval_test
	sh tests/cli_test.sh
	sh tests/morph_whitespace_test.sh
	sh tests/parser_cli_test.sh
	sh tests/check_cli_test.sh
	sh tests/run_cli_test.sh
	sh tests/native_cli_test.sh
	./niyahparse --ast examples/مرحبا.نيّة > tests/parse/مرحبا.actual
	cmp tests/parse/مرحبا.expected tests/parse/مرحبا.actual
	rm -f tests/parse/مرحبا.actual tests/lex/مرحبا.actual

verify-local: test
	sh tests/verify_local.sh

clean:
	rm -f niyahlex niyahparse niyahrun niyahc tests/lexer_test tests/lexer_recovery_test tests/morph_test tests/parser_test tests/check_test tests/eval_test tests/lex/مرحبا.actual tests/parse/مرحبا.actual

.PHONY: all test verify-local clean
