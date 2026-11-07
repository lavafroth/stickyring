build:
    mkdir -p build
    gcc src/main.c -o build/stickyring -luring -s -O2 -Wall

test:
    mkdir -p build
    gcc tests/tests.c -o build/tests
    build/tests
