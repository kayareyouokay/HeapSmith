# HeapSmith

`HeapSmith` is an experimental C memory allocator inspired by allocator internals
and articles such as "Malloc is not magic". It implements a small malloc-family
API, optional debug metadata, allocator integrity checks, leak reporting,
`LD_PRELOAD` interposition, fuzz tests, and a simple benchmark harness.

This repository is intended for learning, experimentation, and allocator design
practice. It is not a production replacement for mature allocators such as glibc
`malloc`, jemalloc, mimalloc, or tcmalloc.

## Features

- `nn_malloc`, `nn_free`, `nn_calloc`, `nn_realloc`, and `nn_reallocarray`
- `nn_posix_memalign`, `nn_aligned_alloc`, and `nn_malloc_usable_size`
- Static library build for direct integration
- Shared library build for `LD_PRELOAD` testing
- Thread-local arenas guarded by per-arena mutexes
- Size-segregated free bins
- Block splitting and coalescing
- Dedicated `mmap` handling for large allocations
- Canary checks around allocations
- Debug allocation metadata with file and line tracking
- Optional debug quarantine for recently freed blocks
- Heap statistics, consistency checking, and leak reporting
- Unit tests, sanitizer tests, preload smoke tests, fuzz tests, and benchmarks

## Repository Layout

```text
.
├── Makefile
├── README.md
├── benchmarks/
│   └── bench_allocator.c
├── include/
│   └── nn_alloc.h
├── src/
│   ├── nn_alloc.c
│   └── nn_preload.c
└── tests/
    ├── fuzz_allocator.c
    ├── preload_hostile.sh
    ├── test_allocator.c
    └── test_preload.c
```

## Requirements

- A C11 compiler such as `cc`, `gcc`, or `clang`
- `make`
- POSIX threads
- A Unix-like system with `mmap`
- Linux-style dynamic loader support for `LD_PRELOAD` if you want to test
  allocator interposition

The default build uses:

```make
CFLAGS=-std=c11 -Wall -Wextra -Wpedantic -O2 -g
LDFLAGS=-pthread
```

You can override these variables when invoking `make`.

## Build

Build the static and shared allocator libraries:

```sh
make
```

This produces:

```text
build/libnnalloc.a
build/libnnalloc.so
```

Clean generated files:

```sh
make clean
```

## Quick Start

Use the allocator directly by including the public header and linking the static
library:

```c
#include "nn_alloc.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    char *message = nn_malloc(64);
    if (message == NULL) {
        return 1;
    }

    strcpy(message, "hello from nn_alloc");
    puts(message);

    nn_free(message);
    return nn_allocator_check() ? 0 : 1;
}
```

Compile after running `make`:

```sh
cc -Iinclude example.c build/libnnalloc.a -pthread -o example
./example
```

## Public API

The public interface lives in [`include/nn_alloc.h`](include/nn_alloc.h).

### Allocation

```c
void *nn_malloc(size_t size);
void nn_free(void *ptr);
void *nn_calloc(size_t count, size_t size);
void *nn_realloc(void *ptr, size_t size);
void *nn_reallocarray(void *ptr, size_t count, size_t size);
```

### Aligned Allocation

```c
int nn_posix_memalign(void **memptr, size_t alignment, size_t size);
void *nn_aligned_alloc(size_t alignment, size_t size);
size_t nn_malloc_usable_size(void *ptr);
```

### Debug Allocation Variants

```c
void *nn_malloc_debug(size_t size, const char *file, int line);
void *nn_calloc_debug(size_t count, size_t size, const char *file, int line);
void *nn_realloc_debug(void *ptr, size_t size, const char *file, int line);
void *nn_reallocarray_debug(void *ptr, size_t count, size_t size,
                            const char *file, int line);
```

Define `NN_ALLOC_ENABLE_MACROS` before including the header to map the normal
`nn_*` allocation calls to their debug variants:

```c
#define NN_ALLOC_ENABLE_MACROS
#include "nn_alloc.h"
```

This records the source file and line for allocations made through the macros,
which is useful with `nn_allocator_dump_leaks`.

### Introspection

```c
typedef struct nn_allocator_stats {
    size_t heap_bytes;
    size_t mapped_bytes;
    size_t used_bytes;
    size_t free_bytes;
    size_t block_count;
    size_t free_block_count;
    size_t arena_count;
    size_t chunk_count;
    size_t large_mapping_count;
    size_t allocation_count;
    size_t failed_allocation_count;
} nn_allocator_stats;

nn_allocator_stats nn_get_allocator_stats(void);
int nn_allocator_check(void);
size_t nn_allocator_dump_leaks(FILE *out);
```

- `nn_get_allocator_stats()` returns aggregate allocator counters and heap
  shape information.
- `nn_allocator_check()` validates known arenas, chunks, blocks, links, and
  canaries.
- `nn_allocator_dump_leaks()` writes live allocation records to the provided
  stream, or to `stderr` when passed `NULL`.

## Using With LD_PRELOAD

The shared object exports standard allocation symbols by forwarding them to
`nn_alloc`:

- `malloc`
- `free`
- `calloc`
- `realloc`
- `reallocarray`
- `posix_memalign`
- `aligned_alloc`
- `malloc_usable_size`
- `valloc`
- `pvalloc`

Build the shared library:

```sh
make build/libnnalloc.so
```

Run a program through the allocator:

```sh
LD_PRELOAD="$PWD/build/libnnalloc.so" ./some_program
```

Run the included preload smoke test:

```sh
make preload-test
```

Run the more hostile preload smoke test against common system tools:

```sh
make hostile-preload-test
```

`LD_PRELOAD` testing is useful for stress and compatibility experiments, but it
also exposes allocator edge cases quickly. Treat failures as debugging data, not
as proof that this allocator is ready for arbitrary real workloads.

## Tests

Run the standard unit test:

```sh
make test
```

Run the debug build test:

```sh
make debug-test
```

Run the AddressSanitizer and UndefinedBehaviorSanitizer test:

```sh
make sanitize-test
```

Run all checks:

```sh
make check-all
```

`check-all` runs:

- Unit tests
- Debug tests
- Sanitizer tests
- `LD_PRELOAD` smoke tests
- Hostile preload smoke tests
- Fuzz tests
- Long fuzz tests
- Sanitizer fuzz tests
- Allocator benchmark
- System allocator benchmark

## Fuzzing

Run the default deterministic fuzz test:

```sh
make fuzz
```

Run the longer fuzz test:

```sh
make long-fuzz
```

The long fuzz target defaults to `250000` steps. Override it with
`LONG_FUZZ_STEPS`:

```sh
make long-fuzz LONG_FUZZ_STEPS=1000000
```

Run the sanitizer fuzz target:

```sh
make sanitize-fuzz
```

## Benchmarks

Run the allocator benchmark:

```sh
make bench
```

Run the same benchmark against the system allocator:

```sh
make bench-system
```

The benchmark performs a deterministic allocation/free workload over a fixed set
of slots and reports elapsed nanoseconds. It is a small smoke benchmark, not a
complete allocator performance suite.

## Design Notes

### Arenas

Each thread lazily receives a thread-local arena. Arenas are linked globally for
introspection and pointer resolution. Each arena owns its chunks, free bins,
large allocation list, and optional debug quarantine.

### Chunks and Blocks

Small and medium allocations come from `mmap`-backed chunks. The default minimum
chunk size is `256 KiB`. Each chunk contains a linked sequence of blocks. Blocks
carry metadata, canaries, physical neighbor links, and free-list links.

### Free Bins

Free blocks are organized into size-segregated bins. Allocation searches from
the requested size class upward and picks a fitting block. Larger free blocks
may be split when the remainder can hold another useful allocation.

### Coalescing

When a block is freed, adjacent free physical neighbors are merged to reduce
fragmentation. In non-debug builds this happens immediately. In debug builds,
recently freed blocks can be quarantined briefly before returning to the free
lists.

### Large Allocations

Allocations at or above `128 KiB` use dedicated `mmap` mappings. Freeing a large
allocation removes it from the arena's large-allocation list and returns the
mapping to the operating system with `munmap`.

### Canaries and Poisoning

Each block has front and back canaries derived from the block address and size.
Allocated memory is filled with `0xa5`; freed small-block memory is filled with
`0xdd`. These patterns are debugging aids and should not be treated as a
security boundary.

## Debugging Leaks

Use the debug allocation variants directly:

```c
void *ptr = nn_malloc_debug(128, __FILE__, __LINE__);
```

Or enable the macros:

```c
#define NN_ALLOC_ENABLE_MACROS
#include "nn_alloc.h"
```

Then dump live allocations:

```c
size_t leaks = nn_allocator_dump_leaks(stderr);
```

Leak records include an allocation id, requested size, and file/line information
when available.

## Error Behavior

- Allocation failures return `NULL` and set `errno` to `ENOMEM`.
- Overflow in `nn_calloc` and `nn_reallocarray` returns `NULL` and sets `errno`
  to `ENOMEM`.
- Invalid frees set `errno` to `EINVAL`.
- Detected canary corruption during `nn_free` sets `errno` to `EFAULT`.
- `nn_posix_memalign` returns `EINVAL` for invalid alignment arguments and
  `ENOMEM` on allocation failure.
- `nn_aligned_alloc` follows the C aligned allocation constraint that `size`
  must be a multiple of `alignment`.

## Build Targets

| Target | Purpose |
| --- | --- |
| `make` / `make all` | Build static and shared libraries |
| `make test` | Build and run the main unit test |
| `make debug-test` | Run tests with `NN_ALLOC_DEBUG` |
| `make sanitize-test` | Run tests with ASan and UBSan |
| `make preload-test` | Run a test binary through `LD_PRELOAD` |
| `make hostile-preload-test` | Run common system tools through `LD_PRELOAD` |
| `make fuzz` | Run deterministic fuzz workload |
| `make long-fuzz` | Run extended deterministic fuzz workload |
| `make sanitize-fuzz` | Run fuzz workload with sanitizers |
| `make bench` | Benchmark `nn_alloc` |
| `make bench-system` | Benchmark the system allocator |
| `make check-all` | Run every test, fuzz, preload, and benchmark target |
| `make clean` | Remove `build/` |

## Limitations

- This allocator is experimental and optimized for readability and learning.
- It has not been hardened for hostile inputs or production workloads.
- The allocator metadata is stored inline with user allocations.
- `LD_PRELOAD` compatibility is intentionally tested but not guaranteed for all
  programs.
- The benchmark is intentionally small and should not be used as the sole basis
  for performance conclusions.
- Platform support is POSIX-oriented and assumes `mmap`, `pthread`, and dynamic
  loader behavior compatible with the current implementation.

## Development Workflow

A practical local loop is:

```sh
make clean
make check-all
```

For allocator changes, run at least:

```sh
make test
make sanitize-test
make fuzz
make preload-test
```

For changes touching pointer resolution, block metadata, coalescing, or preload
symbols, prefer the full `make check-all` target.

## License

No license file is currently included in this repository. Add one before
redistributing or reusing this code outside local experimentation.
