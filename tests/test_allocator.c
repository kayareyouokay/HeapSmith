#include "nn_alloc.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

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
#ifndef NN_ALLOC_DEBUG
    assert(d == b);
#endif

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
#ifndef NN_ALLOC_DEBUG
    assert(joined == a);
#endif

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

static void aligned_allocations_work(void)
{
    void *ptr = NULL;
    int rc = nn_posix_memalign(&ptr, 64, 100);
    assert(rc == 0);
    assert(ptr != NULL);
    assert((uintptr_t)ptr % 64 == 0);
    assert(nn_malloc_usable_size(ptr) >= 100);
    memset(ptr, 1, 100);
    nn_free(ptr);

    ptr = nn_aligned_alloc(4096, 4096);
    assert(ptr != NULL);
    assert((uintptr_t)ptr % 4096 == 0);
    nn_free(ptr);
}

static void reallocarray_checks_overflow(void)
{
    errno = 0;
    void *ptr = nn_reallocarray(NULL, (size_t)-1, 2);
    assert(ptr == NULL);
    assert(errno == ENOMEM);
}

static void leak_report_counts_live_blocks(void)
{
    void *leak = nn_malloc_debug(77, __FILE__, __LINE__);
    assert(leak != NULL);

    FILE *out = tmpfile();
    assert(out != NULL);
    size_t leaks = nn_allocator_dump_leaks(out);
    assert(leaks >= 1);

    fclose(out);
    nn_free(leak);
}

static void *thread_worker(void *arg)
{
    (void)arg;

    for (int i = 0; i < 2000; i++) {
        size_t size = (size_t)((i * 37) % 2048) + 1;
        unsigned char *ptr = nn_malloc(size);
        assert(ptr != NULL);
        memset(ptr, i & 0xff, size);

        if ((i % 3) == 0) {
            unsigned char *bigger = nn_realloc(ptr, size + 128);
            assert(bigger != NULL);
            ptr = bigger;
        }

        nn_free(ptr);
    }

    return NULL;
}

static void threaded_allocations_work(void)
{
    pthread_t threads[4];

    for (size_t i = 0; i < 4; i++) {
        assert(pthread_create(&threads[i], NULL, thread_worker, NULL) == 0);
    }

    for (size_t i = 0; i < 4; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
    }

    nn_allocator_stats stats = nn_get_allocator_stats();
    assert(stats.arena_count >= 4);
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
    aligned_allocations_work();
    reallocarray_checks_overflow();
    leak_report_counts_live_blocks();
    threaded_allocations_work();
    assert(nn_allocator_check());
    return 0;
}
