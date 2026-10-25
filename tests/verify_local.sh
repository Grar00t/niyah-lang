#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/verify-local

# GCC's sanitizer runtime is available on the verified Kali machine.
# A compiler being installed does not imply its ASan runtime is installed.
if command -v gcc >/dev/null 2>&1; then
    gcc -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -o build/verify-local/lexer-gcc-asan tests/lexer_test.c src/lexer.c
    ./build/verify-local/lexer-gcc-asan
    gcc -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -o build/verify-local/lexer-recovery-gcc-asan tests/lexer_recovery_test.c src/lexer.c
    ./build/verify-local/lexer-recovery-gcc-asan
    gcc -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -o build/verify-local/morph-gcc-asan tests/morph_test.c src/morph.c
    ./build/verify-local/morph-gcc-asan
    gcc -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -o build/verify-local/parser-gcc-asan tests/parser_test.c src/parser.c src/lexer.c
    ./build/verify-local/parser-gcc-asan
    echo 'GCC_ASAN_UBSAN_PASS'
else
    echo 'FAIL: gcc is required for the sanitizer acceptance gate' >&2
    exit 1
fi

if command -v clang >/dev/null 2>&1; then
    clang -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
        -o build/verify-local/lexer-clang tests/lexer_test.c src/lexer.c
    ./build/verify-local/lexer-clang
    clang -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
        -o build/verify-local/lexer-recovery-clang tests/lexer_recovery_test.c src/lexer.c
    ./build/verify-local/lexer-recovery-clang
    clang -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
        -o build/verify-local/morph-clang tests/morph_test.c src/morph.c
    ./build/verify-local/morph-clang
    clang -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
        -o build/verify-local/parser-clang tests/parser_test.c src/parser.c src/lexer.c
    ./build/verify-local/parser-clang
    echo 'CLANG_COMPILE_AND_TEST_PASS (Clang sanitizers not claimed)'
fi

if command -v readelf >/dev/null 2>&1; then
    readelf -h niyahlex | grep -Fq 'ELF64'
    readelf -h niyahparse | grep -Fq 'ELF64'
    if readelf -d niyahlex | grep -q 'NEEDED' ||
       readelf -d niyahparse | grep -q 'NEEDED'; then
        echo 'FAIL: dynamic library dependency found' >&2
        exit 1
    fi
    echo 'ELF64_STATIC_CHECK_PASS'
else
    echo 'FAIL: readelf is required to check static ELF' >&2
    exit 1
fi
echo 'LOCAL_VERIFY_PASS'
