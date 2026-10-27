#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/verify-local
frontend="src/check.c src/parser.c src/lexer.c"

verify_compiler() {
    compiler=$1
    label=$2
    extra=$3
    flags="-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror"
    for suite in lexer lexer_recovery morph parser check eval depth; do
        case "$suite" in
            lexer|lexer_recovery) sources="src/lexer.c" ;;
            morph) sources="src/morph.c" ;;
            parser) sources="src/parser.c src/lexer.c" ;;
            check) sources="$frontend" ;;
            eval) sources="src/eval.c $frontend" ;;
            depth) sources="src/eval.c src/codegen.c $frontend" ;;
        esac
        "$compiler" $flags $extra -o "build/verify-local/$suite-$label" \
            "tests/${suite}_test.c" $sources
        "./build/verify-local/$suite-$label"
    done
    "$compiler" $flags $extra -o "build/verify-local/niyahrun-$label" \
        src/run_cli.c src/eval.c $frontend
    "$compiler" $flags $extra -o "build/verify-local/niyahc-$label" \
        src/compile_cli.c src/codegen.c $frontend
    "$compiler" $flags $extra -o "build/verify-local/native-result-probe-$label" \
        tests/native_result_probe.c
    NIYAHRUN="./build/verify-local/niyahrun-$label" sh tests/run_cli_test.sh
    NIYAHRUN="./build/verify-local/niyahrun-$label" \
        NIYAHC="./build/verify-local/niyahc-$label" \
        NATIVE_RESULT_PROBE="./build/verify-local/native-result-probe-$label" \
        sh tests/native_cli_test.sh
}

command -v gcc >/dev/null 2>&1 || {
    echo 'FAIL: gcc is required for the sanitizer gate' >&2
    exit 1
}
verify_compiler gcc gcc-asan '-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
echo 'GCC_ASAN_UBSAN_PASS'

command -v clang >/dev/null 2>&1 || {
    echo 'FAIL: clang is required for the second-compiler gate' >&2
    exit 1
}
verify_compiler clang clang ''
echo 'CLANG_COMPILE_AND_TEST_PASS'

command -v readelf >/dev/null 2>&1 || {
    echo 'FAIL: readelf is required for ELF checks' >&2
    exit 1
}
for binary in niyahlex niyahparse; do
    readelf -h "$binary" | grep -Fq ELF64
    if readelf -d "$binary" | grep -q NEEDED; then
        echo "FAIL: dynamic dependency in $binary" >&2
        exit 1
    fi
done
echo 'ELF64_STATIC_CHECK_PASS'
echo 'LOCAL_VERIFY_PASS'
