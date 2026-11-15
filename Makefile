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

.PHONY: all lib examples test clean

all: lib

lib: $(LIB)

examples: $(EXAMPLE)

test: $(TEST_BIN)
	@INI_TEST_TMPDIR="$(TMP_DIR)" ./$(TEST_BIN)

$(LIB): $(OBJ)
	$(AR) rcs $@ $^

$(OBJ): ini.c ini.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -c ini.c -o $@

$(EXAMPLE): examples/example.c examples/example.ini $(LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -o $@ examples/example.c $(LIB)

$(TEST_BIN): tests/test_ini.c ini.c ini.h | $(BUILD_DIR) $(TMP_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -o $@ tests/test_ini.c ini.c

$(BUILD_DIR):
	@mkdir -p $@

$(TMP_DIR):
	@mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
