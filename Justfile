CC := "gcc"
TARGET := "stickyring"
BUILD_DIR := "build"
CFLAGS := "-luring -s -O2 -Wall -Wextra -Wformat=2 -Wshadow -Wconversion"
SRCS := "src/main.c src/state_machine.h src/state_machine.c"
TEST_SRCS := "tests/tests.c src/state_machine.h src/state_machine.c"

make-build-dir:
    mkdir -p {{BUILD_DIR}}

build: make-build-dir
    mkdir -p build
    {{CC}} {{SRCS}} -o {{BUILD_DIR}}/{{TARGET}} {{CFLAGS}}

test: make-build-dir
    gcc {{TEST_SRCS}} -o {{BUILD_DIR}}/tests
    {{BUILD_DIR}}/tests
