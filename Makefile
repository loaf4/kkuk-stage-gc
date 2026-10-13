# make                    build the collector and the examples into build/
# make gc                 build only build/gc.o, the file to link with a program
# make test               run all tests
# make test-c             run the tests written in C
# make test-stella        run the Stella programs
# make test-one TEST=...  run one test, for example TEST=barrier or TEST=squares
# make clean              remove build/
#
# The tests are built with sanitizers. To build them differently:
#   make test TEST_CFLAGS="-std=c11 -O2"

CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
TEST_CFLAGS ?= -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -fno-sanitize-recover=all

BUILD := build
RUNTIME := runtime/runtime.c runtime/scalar.c runtime/main.c
EXAMPLES := double playlist chat history

.PHONY: all gc examples test test-c test-stella test-one clean

all: gc examples

gc: $(BUILD)/gc.o

examples: $(EXAMPLES:%=$(BUILD)/%)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/gc.o: gc.c internal/gc_stats.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c gc.c -o $@

$(BUILD)/%: examples/%.c $(BUILD)/gc.o $(RUNTIME)
	$(CC) $(CFLAGS) -Iinclude $< $(RUNTIME) $(BUILD)/gc.o -o $@

# tests/run.sh builds the tests itself, into build/tests.
test:
	CC="$(CC)" CFLAGS="$(TEST_CFLAGS)" tests/run.sh

test-c:
	CC="$(CC)" CFLAGS="$(TEST_CFLAGS)" tests/run.sh c

test-stella:
	CC="$(CC)" CFLAGS="$(TEST_CFLAGS)" tests/run.sh stella

test-one:
	CC="$(CC)" CFLAGS="$(TEST_CFLAGS)" tests/run.sh $(TEST)

clean:
	rm -rf $(BUILD)
