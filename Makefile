CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS ?= -Iinclude
LDFLAGS ?= -pthread
SAN_CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -DNN_ALLOC_DEBUG
SAN_LDFLAGS ?= -pthread -fsanitize=address,undefined
LONG_FUZZ_STEPS ?= 250000

BUILD_DIR := build
LIB := $(BUILD_DIR)/libnnalloc.a
DEBUG_LIB := $(BUILD_DIR)/libnnalloc_debug.a
SAN_LIB := $(BUILD_DIR)/libnnalloc_sanitize.a
SHARED := $(BUILD_DIR)/libnnalloc.so
OBJ := $(BUILD_DIR)/nn_alloc.o
DEBUG_OBJ := $(BUILD_DIR)/nn_alloc_debug.o
SAN_OBJ := $(BUILD_DIR)/nn_alloc_sanitize.o
PIC_OBJ := $(BUILD_DIR)/nn_alloc.pic.o
PRELOAD_OBJ := $(BUILD_DIR)/nn_preload.pic.o
TEST := $(BUILD_DIR)/test_allocator
DEBUG_TEST := $(BUILD_DIR)/test_allocator_debug
SAN_TEST := $(BUILD_DIR)/test_allocator_sanitize
PRELOAD_TEST := $(BUILD_DIR)/test_preload
BENCH := $(BUILD_DIR)/bench_allocator
BENCH_SYSTEM := $(BUILD_DIR)/bench_system
FUZZ := $(BUILD_DIR)/fuzz_allocator
LONG_FUZZ := $(BUILD_DIR)/fuzz_allocator_long
SAN_FUZZ := $(BUILD_DIR)/fuzz_allocator_sanitize

.PHONY: all test debug-test sanitize-test preload-test hostile-preload-test fuzz long-fuzz sanitize-fuzz bench bench-system check-all clean

all: $(LIB) $(SHARED)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ): src/nn_alloc.c include/nn_alloc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(DEBUG_OBJ): src/nn_alloc.c include/nn_alloc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DNN_ALLOC_DEBUG -c $< -o $@

$(SAN_OBJ): src/nn_alloc.c include/nn_alloc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(SAN_CFLAGS) -c $< -o $@

$(PIC_OBJ): src/nn_alloc.c include/nn_alloc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

$(PRELOAD_OBJ): src/nn_preload.c include/nn_alloc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

$(LIB): $(OBJ)
	ar rcs $@ $<

$(DEBUG_LIB): $(DEBUG_OBJ)
	ar rcs $@ $<

$(SAN_LIB): $(SAN_OBJ)
	ar rcs $@ $<

$(SHARED): $(PIC_OBJ) $(PRELOAD_OBJ)
	$(CC) -shared $(PIC_OBJ) $(PRELOAD_OBJ) $(LDFLAGS) -o $@

$(TEST): tests/test_allocator.c $(LIB) include/nn_alloc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_allocator.c $(LIB) $(LDFLAGS) -o $@

$(DEBUG_TEST): tests/test_allocator.c $(DEBUG_LIB) include/nn_alloc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DNN_ALLOC_DEBUG tests/test_allocator.c $(DEBUG_LIB) $(LDFLAGS) -o $@

$(SAN_TEST): tests/test_allocator.c $(SAN_LIB) include/nn_alloc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(SAN_CFLAGS) tests/test_allocator.c $(SAN_LIB) $(SAN_LDFLAGS) -o $@

$(PRELOAD_TEST): tests/test_preload.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) tests/test_preload.c -o $@

$(BENCH): benchmarks/bench_allocator.c $(LIB) include/nn_alloc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) benchmarks/bench_allocator.c $(LIB) $(LDFLAGS) -o $@

$(BENCH_SYSTEM): benchmarks/bench_allocator.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DUSE_SYSTEM_MALLOC benchmarks/bench_allocator.c -o $@

$(FUZZ): tests/fuzz_allocator.c $(LIB) include/nn_alloc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/fuzz_allocator.c $(LIB) $(LDFLAGS) -o $@

$(LONG_FUZZ): tests/fuzz_allocator.c $(LIB) include/nn_alloc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DNN_FUZZ_STEPS=$(LONG_FUZZ_STEPS) tests/fuzz_allocator.c $(LIB) $(LDFLAGS) -o $@

$(SAN_FUZZ): tests/fuzz_allocator.c $(SAN_LIB) include/nn_alloc.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(SAN_CFLAGS) tests/fuzz_allocator.c $(SAN_LIB) $(SAN_LDFLAGS) -o $@

test: $(TEST)
	./$(TEST)

debug-test: $(DEBUG_TEST)
	./$(DEBUG_TEST)

sanitize-test: $(SAN_TEST)
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_TEST)

preload-test: $(SHARED) $(PRELOAD_TEST)
	LD_PRELOAD=$(abspath $(SHARED)) ./$(PRELOAD_TEST)

hostile-preload-test: $(SHARED)
	sh tests/preload_hostile.sh $(abspath $(SHARED))

fuzz: $(FUZZ)
	./$(FUZZ)

long-fuzz: $(LONG_FUZZ)
	./$(LONG_FUZZ)

sanitize-fuzz: $(SAN_FUZZ)
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_FUZZ)

bench: $(BENCH)
	./$(BENCH)

bench-system: $(BENCH_SYSTEM)
	./$(BENCH_SYSTEM)

check-all: test debug-test sanitize-test preload-test hostile-preload-test fuzz long-fuzz sanitize-fuzz bench bench-system

clean:
	rm -rf $(BUILD_DIR)
