#include "nn_alloc.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdalign.h>
#include <stddef.h>
#include <string.h>

static void basic_alloc_free(void)
{
    char *buf = nn_malloc(64);
    assert(buf != NULL);
    assert((uintptr_t)buf % alignof(max_align_t) == 0);

    memset(buf, 'a', 64);
    assert(nn_allocator_check());

    nn_allocator_stats stats = nn_get_allocator_stats();
    assert(stats.used_bytes >= 64);
    assert(stats.block_count >= 1);

    nn_free(buf);
    assert(nn_allocator_check());
}

static void reuse_freed_block(void)
{
    void *a = nn_malloc(64);
    void *b = nn_malloc(128);
    void *c = nn_malloc(64);
    assert(a != NULL && b != NULL && c != NULL);

    nn_free(b);
    void *d = nn_malloc(96);
    assert(d == b);

    nn_free(a);
    nn_free(c);
    nn_free(d);
    assert(nn_allocator_check());
}

static void coalesce_neighbors(void)
{
    void *a = nn_malloc(80);
    void *b = nn_malloc(96);
    void *c = nn_malloc(80);
    assert(a != NULL && b != NULL && c != NULL);

    nn_free(b);
    nn_free(a);

    void *joined = nn_malloc(160);
    assert(joined == a);

    nn_free(joined);
    nn_free(c);
    assert(nn_allocator_check());
}

static void calloc_zeroes_memory(void)
{
    int *values = nn_calloc(32, sizeof(int));
    assert(values != NULL);

    for (int i = 0; i < 32; i++) {
        assert(values[i] == 0);
    }

    nn_free(values);
}

static void realloc_keeps_data(void)
{
    char *text = nn_malloc(16);
    assert(text != NULL);
    strcpy(text, "allocator");

    text = nn_realloc(text, 128);
    assert(text != NULL);
    assert(strcmp(text, "allocator") == 0);

    text = nn_realloc(text, 12);
    assert(text != NULL);
    assert(strcmp(text, "allocator") == 0);

    nn_free(text);
}

static void bad_free_does_not_break_heap(void)
{
    int stack_value = 7;
    errno = 0;

    nn_free(&stack_value);
    assert(errno == EINVAL);
    assert(nn_allocator_check());
}

static void large_free_returns_pages(void)
{
    nn_allocator_stats before = nn_get_allocator_stats();
    void *big = nn_malloc(128 * 1024);
    assert(big != NULL);

    nn_allocator_stats during = nn_get_allocator_stats();
    assert(during.heap_bytes > before.heap_bytes);

    nn_free(big);
    nn_allocator_stats after = nn_get_allocator_stats();
    assert(after.heap_bytes < during.heap_bytes);
    assert(nn_allocator_check());
}

int main(void)
{
    basic_alloc_free();
    reuse_freed_block();
    coalesce_neighbors();
    calloc_zeroes_memory();
    realloc_keeps_data();
    bad_free_does_not_break_heap();
    large_free_returns_pages();
    assert(nn_allocator_check());
    return 0;
}
