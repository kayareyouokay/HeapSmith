#ifndef NN_ALLOC_H
#define NN_ALLOC_H

#include <stddef.h>
#include <stdio.h>

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

void *nn_malloc(size_t size);
void *nn_malloc_debug(size_t size, const char *file, int line);
void nn_free(void *ptr);
void *nn_calloc(size_t count, size_t size);
void *nn_calloc_debug(size_t count, size_t size, const char *file, int line);
void *nn_realloc(void *ptr, size_t size);
void *nn_realloc_debug(void *ptr, size_t size, const char *file, int line);
void *nn_reallocarray(void *ptr, size_t count, size_t size);
void *nn_reallocarray_debug(void *ptr, size_t count, size_t size, const char *file, int line);
int nn_posix_memalign(void **memptr, size_t alignment, size_t size);
void *nn_aligned_alloc(size_t alignment, size_t size);
size_t nn_malloc_usable_size(void *ptr);

nn_allocator_stats nn_get_allocator_stats(void);
int nn_allocator_check(void);
size_t nn_allocator_dump_leaks(FILE *out);

#ifdef NN_ALLOC_ENABLE_MACROS
#define nn_malloc(size) nn_malloc_debug((size), __FILE__, __LINE__)
#define nn_calloc(count, size) nn_calloc_debug((count), (size), __FILE__, __LINE__)
#define nn_realloc(ptr, size) nn_realloc_debug((ptr), (size), __FILE__, __LINE__)
#define nn_reallocarray(ptr, count, size) nn_reallocarray_debug((ptr), (count), (size), __FILE__, __LINE__)
#endif

#endif
