CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS ?= -Iinclude
LDFLAGS ?= -pthread

BUILD_DIR := build
LIB := $(BUILD_DIR)/libnnalloc.a
OBJ := $(BUILD_DIR)/nn_alloc.o
TEST := $(BUILD_DIR)/test_allocator

.PHONY: all test clean

all: $(LIB)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ): src/nn_alloc.c include/nn_alloc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ)
	ar rcs $@ $<

$(TEST): tests/test_allocator.c $(LIB) include/nn_alloc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_allocator.c $(LIB) $(LDFLAGS) -o $@

test: $(TEST)
	./$(TEST)

clean:
	rm -rf $(BUILD_DIR)
