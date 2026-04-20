#define _DEFAULT_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef USE_SYSTEM_MALLOC
#include "nn_alloc.h"
#define BENCH_MALLOC nn_malloc
#define BENCH_FREE nn_free
#define BENCH_CHECK() nn_allocator_check()
#define BENCH_STATS() nn_get_allocator_stats()
#else
#define BENCH_MALLOC malloc
#define BENCH_FREE free
#define BENCH_CHECK() 1
#endif

#define OPS 200000
#define SLOTS 4096

static uint64_t ns_since(struct timespec start, struct timespec end)
{
    return (uint64_t)(end.tv_sec - start.tv_sec) * 1000000000ULL +
           (uint64_t)(end.tv_nsec - start.tv_nsec);
}

int main(void)
{
    void *slots[SLOTS] = {0};
    struct timespec start;
    struct timespec end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < OPS; i++) {
        size_t index = (i * 2654435761U) % SLOTS;
        size_t size = ((i * 131U) % 4096U) + 1U;

        if (slots[index] != NULL) {
            BENCH_FREE(slots[index]);
        }
        slots[index] = BENCH_MALLOC(size);
    }

    for (size_t i = 0; i < SLOTS; i++) {
        BENCH_FREE(slots[i]);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

#ifndef USE_SYSTEM_MALLOC
    nn_allocator_stats stats = nn_get_allocator_stats();
    printf("ops=%d time_ns=%llu allocations=%zu mapped=%zu blocks=%zu\n",
           OPS,
           (unsigned long long)ns_since(start, end),
           stats.allocation_count,
           stats.mapped_bytes,
           stats.block_count);
#else
    printf("allocator=system ops=%d time_ns=%llu\n",
           OPS,
           (unsigned long long)ns_since(start, end));
#endif
    return BENCH_CHECK() ? 0 : 1;
}
