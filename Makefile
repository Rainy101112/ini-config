# Makefile for ini-config
#
#   make            build the static library
#   make examples   build the example program
#   make test       build and run the test suite
#   make clean      remove all build artifacts
#
# Everything is written into ./build so the source tree stays clean.

CC         ?= cc
AR         ?= ar
CFLAGS     ?= -O2 -g
# Kept separate from CFLAGS so that `make CFLAGS=...` does not drop the warnings.
WARNFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic
CPPFLAGS   += -I.

BUILD_DIR  := build
TMP_DIR    := $(BUILD_DIR)/tmp
LIB        := $(BUILD_DIR)/libini.a
OBJ        := $(BUILD_DIR)/ini.o
EXAMPLE    := $(BUILD_DIR)/example
TEST_BIN   := $(BUILD_DIR)/test_ini
HASH_BIN   := $(BUILD_DIR)/test_siphash
BENCH_BIN  := $(BUILD_DIR)/bench_ini
BENCH_HEAD := $(BUILD_DIR)/bench_ini_head
FLOOD_BIN  := $(BUILD_DIR)/flood_demo

# Number of keys used by `make bench`.
N ?= 20000

.PHONY: all lib examples test bench bench-HEAD flood-demo clean

all: lib

lib: $(LIB)

examples: $(EXAMPLE)

test: $(TEST_BIN) $(HASH_BIN)
	@INI_TEST_TMPDIR="$(TMP_DIR)" ./$(TEST_BIN)
	@./$(HASH_BIN)

bench: $(BENCH_BIN)
	@INI_TEST_TMPDIR="$(TMP_DIR)" ./$(BENCH_BIN) $(N)

# Same benchmark linked against the ini.c of the last commit, to compare
# implementations without touching the working tree.
bench-HEAD: $(BENCH_HEAD)
	@INI_TEST_TMPDIR="$(TMP_DIR)" ./$(BENCH_HEAD) $(N)

# Shows that a collision flood crafted with a known key is defanged by the
# per-parser random key. Slow (brute forces colliding names), so opt-in.
flood-demo: $(FLOOD_BIN)
	@./$(FLOOD_BIN)

$(LIB): $(OBJ)
	$(AR) rcs $@ $^

$(OBJ): ini.c ini.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -c ini.c -o $@

$(EXAMPLE): examples/example.c examples/example.ini $(LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -o $@ examples/example.c $(LIB)

$(TEST_BIN): tests/test_ini.c ini.c ini.h | $(BUILD_DIR) $(TMP_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -o $@ tests/test_ini.c ini.c

# Includes ini.c itself to reach the static hasher, so it must not also be linked.
$(HASH_BIN): tests/test_siphash.c ini.c ini.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -o $@ tests/test_siphash.c

$(BENCH_BIN): tests/bench_ini.c ini.c ini.h | $(BUILD_DIR) $(TMP_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -o $@ tests/bench_ini.c ini.c

$(FLOOD_BIN): tests/flood_demo.c ini.c ini.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -o $@ tests/flood_demo.c

$(BUILD_DIR)/ini_head.c: ini.c
	git show HEAD:ini.c > $@

$(BENCH_HEAD): tests/bench_ini.c $(BUILD_DIR)/ini_head.c ini.h | $(BUILD_DIR) $(TMP_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -o $@ tests/bench_ini.c $(BUILD_DIR)/ini_head.c

$(BUILD_DIR):
	@mkdir -p $@

$(TMP_DIR):
	@mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
