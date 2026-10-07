# Stage0 is built by the host C compiler. Niyah itself needs no libc and no linker.
CC      ?= cc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wpedantic -ffreestanding -fno-stack-protector \
           -fno-pic -no-pie
LDFLAGS := -static -nostdlib

all: niyahlex

niyahlex: src/lexer.c src/lexer.h
	$(CC) $(CFLAGS) -DNIYAH_LEXER_STANDALONE $(LDFLAGS) -o $@ src/lexer.c

tests/lexer_test: tests/lexer_test.c src/lexer.c src/lexer.h
	$(CC) -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -o $@ tests/lexer_test.c src/lexer.c

test: niyahlex tests/lexer_test
	./niyahlex examples/مرحبا.نيّة > tests/lex/مرحبا.actual
	cmp tests/lex/مرحبا.expected tests/lex/مرحبا.actual
	./tests/lexer_test
	rm -f tests/lex/مرحبا.actual

clean:
	rm -f niyahlex niyah0 niyah1 niyah2 niyah3 tests/lexer_test tests/lex/مرحبا.actual

.PHONY: all test clean
