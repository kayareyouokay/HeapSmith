#define _DEFAULT_SOURCE

#include "nn_alloc.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

void *malloc(size_t size)
{
    return nn_malloc(size);
}

void free(void *ptr)
{
    nn_free(ptr);
}

void *calloc(size_t count, size_t size)
{
    return nn_calloc(count, size);
}

void *realloc(void *ptr, size_t size)
{
    return nn_realloc(ptr, size);
}

void *reallocarray(void *ptr, size_t count, size_t size)
{
    return nn_reallocarray(ptr, count, size);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    return nn_posix_memalign(memptr, alignment, size);
}

void *aligned_alloc(size_t alignment, size_t size)
{
    return nn_aligned_alloc(alignment, size);
}

size_t malloc_usable_size(void *ptr)
{
    return nn_malloc_usable_size(ptr);
}

void *valloc(size_t size)
{
    long page = sysconf(_SC_PAGESIZE);
    void *ptr = NULL;
    int rc = nn_posix_memalign(&ptr, page > 0 ? (size_t)page : 4096, size);

    if (rc != 0) {
        errno = rc;
        return NULL;
    }
    return ptr;
}

void *pvalloc(size_t size)
{
    long raw_page = sysconf(_SC_PAGESIZE);
    size_t page = raw_page > 0 ? (size_t)raw_page : 4096;
    size_t rounded = 0;

    if (size > SIZE_MAX - (page - 1)) {
        errno = ENOMEM;
        return NULL;
    }

    rounded = (size + page - 1) & ~(page - 1);
    return valloc(rounded);
}
