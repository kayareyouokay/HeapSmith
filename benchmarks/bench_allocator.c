#include "nn_alloc.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

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
            nn_free(slots[index]);
        }
        slots[index] = nn_malloc(size);
    }

    for (size_t i = 0; i < SLOTS; i++) {
        nn_free(slots[i]);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    nn_allocator_stats stats = nn_get_allocator_stats();
    printf("ops=%d time_ns=%llu allocations=%zu mapped=%zu blocks=%zu\n",
           OPS,
           (unsigned long long)ns_since(start, end),
           stats.allocation_count,
           stats.mapped_bytes,
           stats.block_count);
    return nn_allocator_check() ? 0 : 1;
}
