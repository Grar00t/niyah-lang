# Stage0 is built by the host C compiler. Niyah itself needs no libc and no linker.
CC      ?= cc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wpedantic -ffreestanding -fno-stack-protector \
           -fno-pic -no-pie
LDFLAGS := -static -nostdlib

all: niyahlex

niyahlex: src/lexer.c src/lexer.h
	$(CC) $(CFLAGS) -DNIYAH_LEXER_STANDALONE $(LDFLAGS) -o $@ src/lexer.c

test: niyahlex
	./niyahlex examples/مرحبا.نيّة

clean:
	rm -f niyahlex niyah0 niyah1 niyah2 niyah3

.PHONY: all test clean
