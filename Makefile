# Stage0 is built by the host C compiler. Niyah itself needs no libc and no linker.
CC      ?= cc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wpedantic -ffreestanding -fno-stack-protector \
           -fno-pic -no-pie
LDFLAGS := -static -nostdlib

all: niyahlex niyahparse

niyahlex: src/lexer.c src/lexer.h src/morph.c src/morph.h
	$(CC) $(CFLAGS) -DNIYAH_LEXER_STANDALONE $(LDFLAGS) -o $@ src/lexer.c src/morph.c

niyahparse: src/parser_cli.c src/parser.c src/parser.h src/lexer.c src/lexer.h
	$(CC) $(CFLAGS) -Werror $(LDFLAGS) -o $@ src/parser_cli.c src/parser.c src/lexer.c

tests/lexer_test: tests/lexer_test.c src/lexer.c src/lexer.h
	$(CC) -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -o $@ tests/lexer_test.c src/lexer.c

tests/lexer_recovery_test: tests/lexer_recovery_test.c src/lexer.c src/lexer.h
	$(CC) -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -o $@ tests/lexer_recovery_test.c src/lexer.c

tests/morph_test: tests/morph_test.c src/morph.c src/morph.h src/lexer.h
	$(CC) -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -o $@ tests/morph_test.c src/morph.c

tests/parser_test: tests/parser_test.c src/parser.c src/parser.h src/lexer.c src/lexer.h
	$(CC) -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -o $@ tests/parser_test.c src/parser.c src/lexer.c

tests/check_test: tests/check_test.c src/check.c src/check.h src/parser.c src/parser.h src/lexer.c src/lexer.h
	$(CC) -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -o $@ tests/check_test.c src/check.c src/parser.c src/lexer.c

test: niyahlex niyahparse tests/lexer_test tests/lexer_recovery_test tests/morph_test tests/parser_test tests/check_test
	./niyahlex examples/مرحبا.نيّة > tests/lex/مرحبا.actual
	cmp tests/lex/مرحبا.expected tests/lex/مرحبا.actual
	./tests/lexer_test
	./tests/lexer_recovery_test
	./tests/morph_test
	./tests/parser_test
	./tests/check_test
	sh tests/cli_test.sh
	sh tests/morph_whitespace_test.sh
	sh tests/parser_cli_test.sh
	./niyahparse --ast examples/مرحبا.نيّة > tests/parse/مرحبا.actual
	cmp tests/parse/مرحبا.expected tests/parse/مرحبا.actual
	rm -f tests/parse/مرحبا.actual
	rm -f tests/lex/مرحبا.actual

verify-local: test
	sh tests/verify_local.sh

clean:
	rm -f niyahlex niyahparse niyah0 niyah1 niyah2 niyah3 tests/lexer_test tests/lexer_recovery_test tests/morph_test tests/parser_test tests/check_test tests/lex/مرحبا.actual tests/parse/مرحبا.actual

.PHONY: all test verify-local clean
