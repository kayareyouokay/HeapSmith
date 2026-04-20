#define _DEFAULT_SOURCE

#include "nn_alloc.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define NN_BLOCK_MAGIC 0x4e4e414c4c4f4342ULL
#define NN_FOOTER_MAGIC 0x4e4e414c4c4f4346ULL

typedef struct nn_block {
    uint64_t magic;
    size_t size;
    size_t requested;
    int free;
    struct nn_block *prev;
    struct nn_block *next;
} nn_block;

static pthread_mutex_t allocator_lock = PTHREAD_MUTEX_INITIALIZER;
static void *heap_base;
static size_t heap_bytes;
static nn_block *head;
static nn_block *tail;
static size_t allocation_count;

static size_t alignment(void)
{
    return _Alignof(max_align_t);
}

static int align_up(size_t value, size_t align, size_t *out)
{
    if (value > SIZE_MAX - (align - 1)) {
        return 0;
    }

    *out = (value + align - 1) & ~(align - 1);
    return 1;
}

static size_t header_size(void)
{
    size_t out = 0;
    (void)align_up(sizeof(nn_block), alignment(), &out);
    return out;
}

static size_t footer_size(void)
{
    size_t out = 0;
    (void)align_up(sizeof(uint64_t), alignment(), &out);
    return out;
}

static size_t page_size(void)
{
    long page = sysconf(_SC_PAGESIZE);
    return page > 0 ? (size_t)page : 4096;
}

static int round_to_pages(size_t value, size_t *out)
{
    return align_up(value, page_size(), out);
}

static int round_down_to_page(size_t value, size_t *out)
{
    size_t page = page_size();
    *out = value - (value % page);
    return 1;
}

static void *block_payload(nn_block *block)
{
    return (char *)block + header_size();
}

static uint64_t footer_value(nn_block *block)
{
    return NN_FOOTER_MAGIC ^ (uint64_t)(uintptr_t)block ^ (uint64_t)block->size;
}

static uint64_t *block_footer(nn_block *block)
{
    return (uint64_t *)((char *)block_payload(block) + block->size);
}

static void write_footer(nn_block *block)
{
    *block_footer(block) = footer_value(block);
}

static size_t block_total_size(nn_block *block)
{
    return header_size() + block->size + footer_size();
}

static int heap_contains_payload(void *ptr)
{
    if (heap_base == NULL) {
        return 0;
    }

    uintptr_t start = (uintptr_t)heap_base;
    uintptr_t end = start + heap_bytes;
    uintptr_t value = (uintptr_t)ptr;
    return value >= start + header_size() && value < end;
}

static int block_is_sane(nn_block *block)
{
    if (heap_base == NULL || block == NULL || block->magic != NN_BLOCK_MAGIC) {
        return 0;
    }

    uintptr_t start = (uintptr_t)heap_base;
    uintptr_t end = start + heap_bytes;
    uintptr_t block_start = (uintptr_t)block;
    size_t total = block_total_size(block);

    if (block_start < start || total > end - block_start) {
        return 0;
    }

    return *block_footer(block) == footer_value(block);
}

static void init_block(nn_block *block, size_t size, int free)
{
    block->magic = NN_BLOCK_MAGIC;
    block->size = size;
    block->requested = 0;
    block->free = free;
    block->prev = NULL;
    block->next = NULL;
    write_footer(block);
}

static int grow_heap(size_t needed)
{
    size_t chunk = 0;
    size_t total_needed = 0;

    if (!align_up(header_size() + footer_size(), alignment(), &total_needed) ||
        needed > SIZE_MAX - total_needed ||
        !round_to_pages(total_needed + needed, &chunk) ||
        chunk > (size_t)INTPTR_MAX) {
        errno = ENOMEM;
        return 0;
    }

    void *region = sbrk((intptr_t)chunk);
    if (region == (void *)-1) {
        errno = ENOMEM;
        return 0;
    }

    if (heap_base == NULL) {
        heap_base = region;
        heap_bytes = chunk;
        head = (nn_block *)region;
        tail = head;
        init_block(head, chunk - header_size() - footer_size(), 1);
        return 1;
    }

    heap_bytes += chunk;
    if (tail != NULL && tail->free) {
        tail->size += chunk;
        write_footer(tail);
        return 1;
    }

    nn_block *block = (nn_block *)region;
    init_block(block, chunk - header_size() - footer_size(), 1);
    block->prev = tail;
    if (tail != NULL) {
        tail->next = block;
    }
    tail = block;
    return 1;
}

static nn_block *best_fit(size_t size)
{
    nn_block *best = NULL;

    for (nn_block *block = head; block != NULL; block = block->next) {
        if (!block_is_sane(block)) {
            return NULL;
        }

        if (block->free && block->size >= size &&
            (best == NULL || block->size < best->size)) {
            best = block;
        }
    }

    return best;
}

static void split_block(nn_block *block, size_t size)
{
    size_t min_payload = alignment() * 2;
    size_t split_cost = header_size() + footer_size() + min_payload;

    if (block->size < size || block->size - size < split_cost) {
        return;
    }

    size_t rest = block->size - size - header_size() - footer_size();
    nn_block *next = (nn_block *)((char *)block_payload(block) + size + footer_size());

    init_block(next, rest, 1);
    next->prev = block;
    next->next = block->next;
    if (next->next != NULL) {
        next->next->prev = next;
    } else {
        tail = next;
    }

    block->next = next;
    block->size = size;
    write_footer(block);
}

static nn_block *coalesce_with_next(nn_block *block)
{
    nn_block *next = block->next;
    if (next == NULL || !next->free || !block_is_sane(next)) {
        return block;
    }

    block->size += header_size() + next->size + footer_size();
    block->next = next->next;
    if (block->next != NULL) {
        block->next->prev = block;
    } else {
        tail = block;
    }
    write_footer(block);
    return block;
}

static nn_block *coalesce(nn_block *block)
{
    block = coalesce_with_next(block);
    if (block->prev != NULL && block->prev->free && block_is_sane(block->prev)) {
        block = block->prev;
        block = coalesce_with_next(block);
    }
    return block;
}

static void trim_heap(void)
{
    if (tail == NULL || !tail->free) {
        return;
    }

    size_t keep = alignment() * 2;
    if (tail->size <= keep) {
        return;
    }

    size_t release = 0;
    (void)round_down_to_page(tail->size - keep, &release);
    if (release == 0 || release > (size_t)INTPTR_MAX) {
        return;
    }

    if (sbrk(-(intptr_t)release) == (void *)-1) {
        return;
    }

    heap_bytes -= release;
    tail->size -= release;
    write_footer(tail);
}

static nn_block *payload_block(void *ptr)
{
    if (!heap_contains_payload(ptr)) {
        return NULL;
    }

    nn_block *block = (nn_block *)((char *)ptr - header_size());
    return block_is_sane(block) ? block : NULL;
}

void *nn_malloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }

    size_t aligned = 0;
    if (!align_up(size, alignment(), &aligned)) {
        errno = ENOMEM;
        return NULL;
    }

    pthread_mutex_lock(&allocator_lock);

    if (head == NULL && !grow_heap(aligned)) {
        pthread_mutex_unlock(&allocator_lock);
        return NULL;
    }

    nn_block *block = best_fit(aligned);
    if (block == NULL && grow_heap(aligned)) {
        block = best_fit(aligned);
    }

    if (block == NULL) {
        pthread_mutex_unlock(&allocator_lock);
        errno = ENOMEM;
        return NULL;
    }

    split_block(block, aligned);
    block->free = 0;
    block->requested = size;
    write_footer(block);
    allocation_count++;

    void *payload = block_payload(block);
    pthread_mutex_unlock(&allocator_lock);
    return payload;
}

void nn_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    pthread_mutex_lock(&allocator_lock);

    nn_block *block = payload_block(ptr);
    if (block == NULL || block->free) {
        errno = EINVAL;
        pthread_mutex_unlock(&allocator_lock);
        return;
    }

    block->free = 1;
    block->requested = 0;
    block = coalesce(block);
    trim_heap();

    pthread_mutex_unlock(&allocator_lock);
}

void *nn_calloc(size_t count, size_t size)
{
    if (count != 0 && size > SIZE_MAX / count) {
        errno = ENOMEM;
        return NULL;
    }

    size_t total = count * size;
    void *ptr = nn_malloc(total);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *nn_realloc(void *ptr, size_t size)
{
    if (ptr == NULL) {
        return nn_malloc(size);
    }

    if (size == 0) {
        nn_free(ptr);
        return NULL;
    }

    size_t aligned = 0;
    if (!align_up(size, alignment(), &aligned)) {
        errno = ENOMEM;
        return NULL;
    }

    pthread_mutex_lock(&allocator_lock);

    nn_block *block = payload_block(ptr);
    if (block == NULL || block->free) {
        errno = EINVAL;
        pthread_mutex_unlock(&allocator_lock);
        return NULL;
    }

    size_t old_requested = block->requested;
    if (aligned <= block->size) {
        split_block(block, aligned);
        block->requested = size;
        write_footer(block);
        pthread_mutex_unlock(&allocator_lock);
        return ptr;
    }

    if (block->next != NULL && block->next->free &&
        block->size + header_size() + block->next->size + footer_size() >= aligned) {
        block = coalesce_with_next(block);
        block->free = 0;
        split_block(block, aligned);
        block->requested = size;
        write_footer(block);
        pthread_mutex_unlock(&allocator_lock);
        return block_payload(block);
    }

    pthread_mutex_unlock(&allocator_lock);

    void *new_ptr = nn_malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }

    memcpy(new_ptr, ptr, old_requested < size ? old_requested : size);
    nn_free(ptr);
    return new_ptr;
}

nn_allocator_stats nn_get_allocator_stats(void)
{
    nn_allocator_stats stats = {0};

    pthread_mutex_lock(&allocator_lock);
    stats.heap_bytes = heap_bytes;
    stats.allocation_count = allocation_count;

    for (nn_block *block = head; block != NULL; block = block->next) {
        if (!block_is_sane(block)) {
            break;
        }

        stats.block_count++;
        if (block->free) {
            stats.free_block_count++;
            stats.free_bytes += block->size;
        } else {
            stats.used_bytes += block->requested;
        }
    }

    pthread_mutex_unlock(&allocator_lock);
    return stats;
}

int nn_allocator_check(void)
{
    int ok = 1;

    pthread_mutex_lock(&allocator_lock);

    nn_block *prev = NULL;
    for (nn_block *block = head; block != NULL; block = block->next) {
        if (block->prev != prev || !block_is_sane(block)) {
            ok = 0;
            break;
        }
        prev = block;
    }

    if (prev != tail) {
        ok = 0;
    }

    pthread_mutex_unlock(&allocator_lock);
    return ok;
}
