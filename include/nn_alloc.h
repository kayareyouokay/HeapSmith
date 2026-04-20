#ifndef NN_ALLOC_H
#define NN_ALLOC_H

#include <stddef.h>

typedef struct nn_allocator_stats {
    size_t heap_bytes;
    size_t used_bytes;
    size_t free_bytes;
    size_t block_count;
    size_t free_block_count;
    size_t allocation_count;
} nn_allocator_stats;

void *nn_malloc(size_t size);
void nn_free(void *ptr);
void *nn_calloc(size_t count, size_t size);
void *nn_realloc(void *ptr, size_t size);

nn_allocator_stats nn_get_allocator_stats(void);
int nn_allocator_check(void);

#endif
