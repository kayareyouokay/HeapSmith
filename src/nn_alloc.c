#include "nn_alloc.h"

#include <errno.h>
#include <stdint.h>

void *nn_malloc(size_t size)
{
    (void)size;
    errno = ENOMEM;
    return NULL;
}

void nn_free(void *ptr)
{
    (void)ptr;
}

void *nn_calloc(size_t count, size_t size)
{
    (void)count;
    (void)size;
    errno = ENOMEM;
    return NULL;
}

void *nn_realloc(void *ptr, size_t size)
{
    (void)ptr;
    (void)size;
    errno = ENOMEM;
    return NULL;
}

nn_allocator_stats nn_get_allocator_stats(void)
{
    return (nn_allocator_stats){0};
}

int nn_allocator_check(void)
{
    return 1;
}
