#define _DEFAULT_SOURCE

#include "nn_alloc.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define NN_BLOCK_MAGIC 0x4e4e414c4c4f4342ULL
#define NN_CHUNK_MAGIC 0x4e4e414c43484e4bULL
#define NN_ARENA_MAGIC 0x4e4e414c4152454eULL
#define NN_PREFIX_MAGIC 0x4e4e414c414c4947ULL
#define NN_FRONT_CANARY 0xace0ace0ace0ace0ULL
#define NN_BACK_CANARY 0xbad0bad0bad0bad0ULL

#define NN_BIN_COUNT 14
#define NN_MIN_CHUNK_SIZE (256U * 1024U)
#define NN_LARGE_THRESHOLD (128U * 1024U)
#define NN_ALLOC_POISON 0xa5
#define NN_FREE_POISON 0xdd

#ifdef NN_ALLOC_DEBUG
#define NN_QUARANTINE_LIMIT 8
#else
#define NN_QUARANTINE_LIMIT 0
#endif

typedef enum nn_block_state {
    NN_BLOCK_FREE = 0,
    NN_BLOCK_USED = 1,
    NN_BLOCK_QUARANTINED = 2
} nn_block_state;

typedef struct nn_arena nn_arena;
typedef struct nn_chunk nn_chunk;
typedef struct nn_block nn_block;

typedef struct nn_aligned_prefix {
    uint64_t magic;
    void *base;
    size_t requested;
} nn_aligned_prefix;

struct nn_block {
    uint64_t magic;
    size_t size;
    size_t requested;
    size_t map_size;
    nn_block_state state;
    unsigned flags;
    nn_arena *arena;
    nn_chunk *chunk;
    nn_block *prev_phys;
    nn_block *next_phys;
    nn_block *prev_free;
    nn_block *next_free;
    nn_block *prev_large;
    nn_block *next_large;
    nn_block *prev_quarantine;
    nn_block *next_quarantine;
    const char *file;
    int line;
    uint64_t alloc_id;
};

struct nn_chunk {
    uint64_t magic;
    size_t map_size;
    nn_arena *arena;
    nn_chunk *next;
    nn_block *first;
};

struct nn_arena {
    uint64_t magic;
    pthread_mutex_t lock;
    nn_arena *next;
    nn_chunk *chunks;
    nn_block *bins[NN_BIN_COUNT];
    nn_block *large_head;
    nn_block *quarantine_head;
    nn_block *quarantine_tail;
    size_t quarantine_count;
};

typedef struct nn_ptr_info {
    nn_block *block;
    void *user_ptr;
    size_t requested;
    int aligned;
} nn_ptr_info;

static pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;
static nn_arena *arenas;
static _Thread_local nn_arena *thread_arena;
static size_t total_allocations;
static size_t failed_allocations;
static uint64_t next_alloc_id = 1;

static uint64_t next_id(void)
{
    return __sync_fetch_and_add(&next_alloc_id, 1);
}

static void record_allocation(void)
{
    (void)__sync_add_and_fetch(&total_allocations, 1);
}

static void record_failed_allocation(void)
{
    (void)__sync_add_and_fetch(&failed_allocations, 1);
}

static size_t alignment(void)
{
    return _Alignof(max_align_t);
}

static size_t page_size(void)
{
    long page = sysconf(_SC_PAGESIZE);
    return page > 0 ? (size_t)page : 4096;
}

static int is_power_of_two(size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static int align_up(size_t value, size_t align, size_t *out)
{
    if (!is_power_of_two(align) || value > SIZE_MAX - (align - 1)) {
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

static size_t chunk_header_size(void)
{
    size_t out = 0;
    (void)align_up(sizeof(nn_chunk), alignment(), &out);
    return out;
}

static size_t guard_size(void)
{
    return alignment();
}

static size_t block_overhead(void)
{
    return header_size() + guard_size() + guard_size();
}

static size_t block_total_size_for(size_t size)
{
    return block_overhead() + size;
}

static void *block_payload(nn_block *block)
{
    return (char *)block + header_size() + guard_size();
}

static uint64_t *front_canary(nn_block *block)
{
    return (uint64_t *)((char *)block + header_size());
}

static uint64_t *back_canary(nn_block *block)
{
    return (uint64_t *)((char *)block_payload(block) + block->size);
}

static uint64_t canary_value(uint64_t seed, nn_block *block)
{
    return seed ^ (uint64_t)(uintptr_t)block ^ (uint64_t)block->size;
}

static void write_canaries(nn_block *block)
{
    *front_canary(block) = canary_value(NN_FRONT_CANARY, block);
    *back_canary(block) = canary_value(NN_BACK_CANARY, block);
}

static int canaries_ok(nn_block *block)
{
    return *front_canary(block) == canary_value(NN_FRONT_CANARY, block) &&
           *back_canary(block) == canary_value(NN_BACK_CANARY, block);
}

static int round_to_pages(size_t value, size_t *out)
{
    return align_up(value, page_size(), out);
}

static void *map_pages(size_t size)
{
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return ptr == MAP_FAILED ? NULL : ptr;
}

static int bin_index(size_t size)
{
    size_t class_size = 16;

    for (int i = 0; i < NN_BIN_COUNT - 1; i++) {
        if (size <= class_size) {
            return i;
        }
        class_size <<= 1;
    }

    return NN_BIN_COUNT - 1;
}

static void init_block(nn_arena *arena, nn_chunk *chunk, nn_block *block, size_t size)
{
    memset(block, 0, sizeof(*block));
    block->magic = NN_BLOCK_MAGIC;
    block->size = size;
    block->state = NN_BLOCK_FREE;
    block->arena = arena;
    block->chunk = chunk;
    write_canaries(block);
}

static void insert_free(nn_arena *arena, nn_block *block)
{
    int index = bin_index(block->size);

    block->state = NN_BLOCK_FREE;
    block->prev_free = NULL;
    block->next_free = arena->bins[index];
    if (block->next_free != NULL) {
        block->next_free->prev_free = block;
    }
    arena->bins[index] = block;
}

static void remove_free(nn_arena *arena, nn_block *block)
{
    int index = bin_index(block->size);

    if (block->prev_free != NULL) {
        block->prev_free->next_free = block->next_free;
    } else if (arena->bins[index] == block) {
        arena->bins[index] = block->next_free;
    }

    if (block->next_free != NULL) {
        block->next_free->prev_free = block->prev_free;
    }

    block->prev_free = NULL;
    block->next_free = NULL;
}

static int block_within_chunk(nn_block *block)
{
    if (block == NULL || block->chunk == NULL || block->chunk->magic != NN_CHUNK_MAGIC) {
        return 0;
    }

    uintptr_t start = (uintptr_t)block->chunk;
    uintptr_t end = start + block->chunk->map_size;
    uintptr_t value = (uintptr_t)block;
    size_t total = block_total_size_for(block->size);

    return value >= start + chunk_header_size() && total <= end - value;
}

static int block_is_sane(nn_block *block)
{
    if (block == NULL || block->magic != NN_BLOCK_MAGIC) {
        return 0;
    }

    if ((block->flags & 1U) != 0) {
        uintptr_t value = (uintptr_t)block;
        return block->map_size >= block_total_size_for(block->size) &&
               canaries_ok(block) &&
               value % alignment() == 0;
    }

    return block_within_chunk(block) && canaries_ok(block);
}

static nn_arena *create_arena(void)
{
    size_t size = 0;
    if (!round_to_pages(sizeof(nn_arena), &size)) {
        errno = ENOMEM;
        return NULL;
    }

    nn_arena *arena = map_pages(size);
    if (arena == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    memset(arena, 0, sizeof(*arena));
    arena->magic = NN_ARENA_MAGIC;
    pthread_mutex_init(&arena->lock, NULL);

    pthread_mutex_lock(&global_lock);
    arena->next = arenas;
    arenas = arena;
    pthread_mutex_unlock(&global_lock);

    return arena;
}

static nn_arena *current_arena(void)
{
    if (thread_arena == NULL) {
        thread_arena = create_arena();
    }
    return thread_arena;
}

static int add_chunk(nn_arena *arena, size_t needed)
{
    size_t chunk_size = NN_MIN_CHUNK_SIZE;
    size_t wanted = chunk_header_size() + block_total_size_for(needed);

    if (wanted > chunk_size && !round_to_pages(wanted, &chunk_size)) {
        errno = ENOMEM;
        return 0;
    }

    nn_chunk *chunk = map_pages(chunk_size);
    if (chunk == NULL) {
        errno = ENOMEM;
        return 0;
    }

    memset(chunk, 0, sizeof(*chunk));
    chunk->magic = NN_CHUNK_MAGIC;
    chunk->map_size = chunk_size;
    chunk->arena = arena;
    chunk->next = arena->chunks;
    arena->chunks = chunk;

    nn_block *block = (nn_block *)((char *)chunk + chunk_header_size());
    size_t payload_size = chunk_size - chunk_header_size() - block_overhead();
    chunk->first = block;
    init_block(arena, chunk, block, payload_size);
    insert_free(arena, block);
    return 1;
}

static nn_block *find_free_block(nn_arena *arena, size_t size)
{
    nn_block *best = NULL;

    for (int i = bin_index(size); i < NN_BIN_COUNT; i++) {
        for (nn_block *block = arena->bins[i]; block != NULL; block = block->next_free) {
            if (block->size >= size && (best == NULL || block->size < best->size)) {
                best = block;
            }
        }
        if (best != NULL) {
            return best;
        }
    }

    return NULL;
}

static void split_block(nn_arena *arena, nn_block *block, size_t size)
{
    size_t min_payload = alignment() * 2;

    if (block->size < size ||
        block->size - size < block_overhead() + min_payload) {
        return;
    }

    size_t rest = block->size - size - block_overhead();
    nn_block *next = (nn_block *)((char *)block + block_total_size_for(size));
    init_block(arena, block->chunk, next, rest);

    next->prev_phys = block;
    next->next_phys = block->next_phys;
    if (next->next_phys != NULL) {
        next->next_phys->prev_phys = next;
    }

    block->next_phys = next;
    block->size = size;
    write_canaries(block);
    insert_free(arena, next);
}

static nn_block *merge_with_next(nn_arena *arena, nn_block *block)
{
    nn_block *next = block->next_phys;
    if (next == NULL || next->state != NN_BLOCK_FREE || !block_is_sane(next)) {
        return block;
    }

    remove_free(arena, next);
    block->size += block_overhead() + next->size;
    block->next_phys = next->next_phys;
    if (block->next_phys != NULL) {
        block->next_phys->prev_phys = block;
    }
    write_canaries(block);
    return block;
}

static nn_block *coalesce_free(nn_arena *arena, nn_block *block)
{
    block = merge_with_next(arena, block);
    if (block->prev_phys != NULL && block->prev_phys->state == NN_BLOCK_FREE &&
        block_is_sane(block->prev_phys)) {
        nn_block *prev = block->prev_phys;
        remove_free(arena, prev);
        prev->size += block_overhead() + block->size;
        prev->next_phys = block->next_phys;
        if (prev->next_phys != NULL) {
            prev->next_phys->prev_phys = prev;
        }
        block = prev;
        write_canaries(block);
        block = merge_with_next(arena, block);
    }

    return block;
}

static void quarantine_push(nn_arena *arena, nn_block *block)
{
    block->prev_quarantine = arena->quarantine_tail;
    block->next_quarantine = NULL;
    if (arena->quarantine_tail != NULL) {
        arena->quarantine_tail->next_quarantine = block;
    } else {
        arena->quarantine_head = block;
    }
    arena->quarantine_tail = block;
    arena->quarantine_count++;
}

static nn_block *quarantine_pop(nn_arena *arena)
{
    nn_block *block = arena->quarantine_head;
    if (block == NULL) {
        return NULL;
    }

    arena->quarantine_head = block->next_quarantine;
    if (arena->quarantine_head != NULL) {
        arena->quarantine_head->prev_quarantine = NULL;
    } else {
        arena->quarantine_tail = NULL;
    }
    block->prev_quarantine = NULL;
    block->next_quarantine = NULL;
    arena->quarantine_count--;
    return block;
}

static void release_quarantined(nn_arena *arena)
{
    nn_block *block = quarantine_pop(arena);
    if (block == NULL || !block_is_sane(block)) {
        return;
    }

    block->state = NN_BLOCK_FREE;
    block->requested = 0;
    block = coalesce_free(arena, block);
    insert_free(arena, block);
}

static void drain_quarantine(nn_arena *arena)
{
    while (arena->quarantine_head != NULL) {
        release_quarantined(arena);
    }
}

static void mark_used(nn_block *block, size_t requested, const char *file, int line)
{
    block->state = NN_BLOCK_USED;
    block->requested = requested;
    block->file = file;
    block->line = line;
    block->alloc_id = next_id();
    write_canaries(block);
    memset(block_payload(block), NN_ALLOC_POISON, block->size);
}

static void *arena_malloc(size_t size, const char *file, int line)
{
    nn_arena *arena = current_arena();
    if (arena == NULL) {
        record_failed_allocation();
        return NULL;
    }

    size_t aligned = 0;
    if (!align_up(size, alignment(), &aligned)) {
        errno = ENOMEM;
        record_failed_allocation();
        return NULL;
    }

    pthread_mutex_lock(&arena->lock);

    nn_block *block = find_free_block(arena, aligned);
    if (block == NULL && arena->quarantine_head != NULL) {
        drain_quarantine(arena);
        block = find_free_block(arena, aligned);
    }
    if (block == NULL && add_chunk(arena, aligned)) {
        block = find_free_block(arena, aligned);
    }
    if (block == NULL) {
        pthread_mutex_unlock(&arena->lock);
        errno = ENOMEM;
        record_failed_allocation();
        return NULL;
    }

    remove_free(arena, block);
    split_block(arena, block, aligned);
    mark_used(block, size, file, line);
    record_allocation();

    void *payload = block_payload(block);
    pthread_mutex_unlock(&arena->lock);
    return payload;
}

static void large_list_insert(nn_arena *arena, nn_block *block)
{
    block->next_large = arena->large_head;
    block->prev_large = NULL;
    if (arena->large_head != NULL) {
        arena->large_head->prev_large = block;
    }
    arena->large_head = block;
}

static void large_list_remove(nn_arena *arena, nn_block *block)
{
    if (block->prev_large != NULL) {
        block->prev_large->next_large = block->next_large;
    } else {
        arena->large_head = block->next_large;
    }
    if (block->next_large != NULL) {
        block->next_large->prev_large = block->prev_large;
    }
}

static void *large_malloc(size_t size, size_t align, const char *file, int line)
{
    nn_arena *arena = current_arena();
    size_t aligned = 0;
    size_t map_size = 0;
    size_t extra = align + sizeof(nn_aligned_prefix);

    if (arena == NULL ||
        !align_up(size, alignment(), &aligned) ||
        aligned > SIZE_MAX - block_overhead() - extra ||
        !round_to_pages(block_overhead() + aligned + extra, &map_size)) {
        errno = ENOMEM;
        record_failed_allocation();
        return NULL;
    }

    nn_block *block = map_pages(map_size);
    if (block == NULL) {
        errno = ENOMEM;
        record_failed_allocation();
        return NULL;
    }

    init_block(arena, NULL, block, map_size - block_overhead());
    block->flags = 1U;
    block->map_size = map_size;
    mark_used(block, size, file, line);

    pthread_mutex_lock(&arena->lock);
    large_list_insert(arena, block);
    record_allocation();
    pthread_mutex_unlock(&arena->lock);

    if (align <= alignment()) {
        return block_payload(block);
    }

    uintptr_t raw = (uintptr_t)block_payload(block) + sizeof(nn_aligned_prefix);
    uintptr_t aligned_ptr = (raw + align - 1) & ~(uintptr_t)(align - 1);
    nn_aligned_prefix *prefix = (nn_aligned_prefix *)(aligned_ptr - sizeof(*prefix));
    prefix->magic = NN_PREFIX_MAGIC;
    prefix->base = block_payload(block);
    prefix->requested = size;
    return (void *)aligned_ptr;
}

static int ptr_in_large(nn_block *block, void *ptr)
{
    uintptr_t value = (uintptr_t)ptr;
    uintptr_t start = (uintptr_t)block;
    uintptr_t end = start + block->map_size;
    return value > start && value < end;
}

static int resolve_in_block(nn_block *block, void *ptr, nn_ptr_info *info)
{
    void *payload = block_payload(block);
    uintptr_t value = (uintptr_t)ptr;
    uintptr_t payload_start = (uintptr_t)payload;
    uintptr_t payload_end = payload_start + block->size;

    if (!block_is_sane(block)) {
        return 0;
    }

    if (ptr == payload) {
        info->block = block;
        info->user_ptr = ptr;
        info->requested = block->requested;
        info->aligned = 0;
        return 1;
    }

    if (value >= payload_start + sizeof(nn_aligned_prefix) && value < payload_end) {
        nn_aligned_prefix *prefix = (nn_aligned_prefix *)(value - sizeof(*prefix));
        if (prefix->magic == NN_PREFIX_MAGIC && prefix->base == payload) {
            info->block = block;
            info->user_ptr = ptr;
            info->requested = prefix->requested;
            info->aligned = 1;
            return 1;
        }
    }

    return 0;
}

static int lock_resolved_block(nn_block *block, void *ptr, nn_ptr_info *info)
{
    nn_arena *arena = block->arena;

    if (arena == NULL || arena->magic != NN_ARENA_MAGIC) {
        return 0;
    }

    pthread_mutex_lock(&arena->lock);
    if (resolve_in_block(block, ptr, info)) {
        return 1;
    }
    pthread_mutex_unlock(&arena->lock);
    return 0;
}

static int resolve_ptr_fast(void *ptr, nn_ptr_info *info)
{
    uintptr_t value = (uintptr_t)ptr;

    if ((value % alignment()) != 0) {
        return 0;
    }

    nn_block *direct = (nn_block *)(value - header_size() - guard_size());
    if (direct->magic == NN_BLOCK_MAGIC && lock_resolved_block(direct, ptr, info)) {
        return 1;
    }

    nn_aligned_prefix *prefix = (nn_aligned_prefix *)(value - sizeof(*prefix));
    if (prefix->magic == NN_PREFIX_MAGIC && prefix->base != NULL) {
        nn_block *aligned = (nn_block *)((char *)prefix->base - header_size() - guard_size());
        if (aligned->magic == NN_BLOCK_MAGIC && lock_resolved_block(aligned, ptr, info)) {
            return 1;
        }
    }

    return 0;
}

static int resolve_ptr_locked(void *ptr, nn_ptr_info *info)
{
    memset(info, 0, sizeof(*info));

    if (resolve_ptr_fast(ptr, info)) {
        return 1;
    }

    pthread_mutex_lock(&global_lock);
    for (nn_arena *arena = arenas; arena != NULL; arena = arena->next) {
        pthread_mutex_lock(&arena->lock);

        for (nn_block *large = arena->large_head; large != NULL; large = large->next_large) {
            if (ptr_in_large(large, ptr) && resolve_in_block(large, ptr, info)) {
                pthread_mutex_unlock(&global_lock);
                return 1;
            }
        }

        for (nn_chunk *chunk = arena->chunks; chunk != NULL; chunk = chunk->next) {
            uintptr_t value = (uintptr_t)ptr;
            uintptr_t start = (uintptr_t)chunk;
            uintptr_t end = start + chunk->map_size;
            if (value <= start || value >= end) {
                continue;
            }

            for (nn_block *block = chunk->first; block != NULL; block = block->next_phys) {
                if (resolve_in_block(block, ptr, info)) {
                    pthread_mutex_unlock(&global_lock);
                    return 1;
                }
            }
        }

        pthread_mutex_unlock(&arena->lock);
    }
    pthread_mutex_unlock(&global_lock);
    return 0;
}

void *nn_malloc_debug(size_t size, const char *file, int line)
{
    if (size == 0) {
        return NULL;
    }

    if (size >= NN_LARGE_THRESHOLD) {
        return large_malloc(size, alignment(), file, line);
    }

    return arena_malloc(size, file, line);
}

void *nn_malloc(size_t size)
{
    return nn_malloc_debug(size, NULL, 0);
}

void nn_free(void *ptr)
{
    nn_ptr_info info;

    if (ptr == NULL) {
        return;
    }

    if (!resolve_ptr_locked(ptr, &info) || info.block->state != NN_BLOCK_USED) {
        errno = EINVAL;
        return;
    }

    nn_block *block = info.block;
    nn_arena *arena = block->arena;

    if (!canaries_ok(block)) {
        errno = EFAULT;
        pthread_mutex_unlock(&arena->lock);
        return;
    }

    if ((block->flags & 1U) != 0) {
        large_list_remove(arena, block);
        size_t map_size = block->map_size;
        pthread_mutex_unlock(&arena->lock);
        munmap(block, map_size);
        return;
    }

    memset(block_payload(block), NN_FREE_POISON, block->size);
    block->state = NN_BLOCK_QUARANTINED;
    block->requested = 0;
    block->file = NULL;
    block->line = 0;

    if (NN_QUARANTINE_LIMIT > 0) {
        quarantine_push(arena, block);
        while (arena->quarantine_count > NN_QUARANTINE_LIMIT) {
            release_quarantined(arena);
        }
    } else {
        block->state = NN_BLOCK_FREE;
        block = coalesce_free(arena, block);
        insert_free(arena, block);
    }

    pthread_mutex_unlock(&arena->lock);
}

void *nn_calloc_debug(size_t count, size_t size, const char *file, int line)
{
    if (count != 0 && size > SIZE_MAX / count) {
        errno = ENOMEM;
        record_failed_allocation();
        return NULL;
    }

    size_t total = count * size;
    void *ptr = nn_malloc_debug(total, file, line);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *nn_calloc(size_t count, size_t size)
{
    return nn_calloc_debug(count, size, NULL, 0);
}

void *nn_realloc_debug(void *ptr, size_t size, const char *file, int line)
{
    nn_ptr_info info;

    if (ptr == NULL) {
        return nn_malloc_debug(size, file, line);
    }

    if (size == 0) {
        nn_free(ptr);
        return NULL;
    }

    if (!resolve_ptr_locked(ptr, &info) || info.block->state != NN_BLOCK_USED) {
        errno = EINVAL;
        return NULL;
    }

    nn_block *block = info.block;
    nn_arena *arena = block->arena;
    size_t old_requested = info.requested;

    if (!info.aligned && (block->flags & 1U) == 0) {
        size_t aligned = 0;
        if (align_up(size, alignment(), &aligned) && aligned <= block->size) {
            split_block(arena, block, aligned);
            block->requested = size;
            block->file = file;
            block->line = line;
            write_canaries(block);
            pthread_mutex_unlock(&arena->lock);
            return ptr;
        }

        if (align_up(size, alignment(), &aligned) &&
            block->next_phys != NULL &&
            block->next_phys->state == NN_BLOCK_FREE &&
            block->size + block_overhead() + block->next_phys->size >= aligned) {
            block = merge_with_next(arena, block);
            split_block(arena, block, aligned);
            block->state = NN_BLOCK_USED;
            block->requested = size;
            block->file = file;
            block->line = line;
            write_canaries(block);
            pthread_mutex_unlock(&arena->lock);
            return block_payload(block);
        }
    }

    pthread_mutex_unlock(&arena->lock);

    void *new_ptr = nn_malloc_debug(size, file, line);
    if (new_ptr == NULL) {
        return NULL;
    }

    memcpy(new_ptr, ptr, old_requested < size ? old_requested : size);
    nn_free(ptr);
    return new_ptr;
}

void *nn_realloc(void *ptr, size_t size)
{
    return nn_realloc_debug(ptr, size, NULL, 0);
}

void *nn_reallocarray_debug(void *ptr, size_t count, size_t size, const char *file, int line)
{
    if (count != 0 && size > SIZE_MAX / count) {
        errno = ENOMEM;
        record_failed_allocation();
        return NULL;
    }

    return nn_realloc_debug(ptr, count * size, file, line);
}

void *nn_reallocarray(void *ptr, size_t count, size_t size)
{
    return nn_reallocarray_debug(ptr, count, size, NULL, 0);
}

int nn_posix_memalign(void **memptr, size_t align, size_t size)
{
    if (memptr == NULL || align < sizeof(void *) || !is_power_of_two(align)) {
        return EINVAL;
    }

    void *ptr = large_malloc(size == 0 ? 1 : size, align, NULL, 0);
    if (ptr == NULL) {
        return ENOMEM;
    }

    *memptr = ptr;
    return 0;
}

void *nn_aligned_alloc(size_t align, size_t size)
{
    void *ptr = NULL;

    if (align < sizeof(void *) || !is_power_of_two(align) || size % align != 0) {
        errno = EINVAL;
        return NULL;
    }

    int rc = nn_posix_memalign(&ptr, align, size);
    if (rc != 0) {
        errno = rc;
        return NULL;
    }

    return ptr;
}

size_t nn_malloc_usable_size(void *ptr)
{
    nn_ptr_info info;

    if (ptr == NULL || !resolve_ptr_locked(ptr, &info) || info.block->state != NN_BLOCK_USED) {
        return 0;
    }

    size_t usable = info.aligned ? info.requested : info.block->size;
    pthread_mutex_unlock(&info.block->arena->lock);
    return usable;
}

nn_allocator_stats nn_get_allocator_stats(void)
{
    nn_allocator_stats stats = {0};

    pthread_mutex_lock(&global_lock);
    stats.allocation_count = __sync_fetch_and_add(&total_allocations, 0);
    stats.failed_allocation_count = __sync_fetch_and_add(&failed_allocations, 0);

    for (nn_arena *arena = arenas; arena != NULL; arena = arena->next) {
        stats.arena_count++;
        pthread_mutex_lock(&arena->lock);

        for (nn_block *large = arena->large_head; large != NULL; large = large->next_large) {
            stats.large_mapping_count++;
            stats.mapped_bytes += large->map_size;
            stats.heap_bytes += large->map_size;
            if (large->state == NN_BLOCK_USED) {
                stats.used_bytes += large->requested;
                stats.block_count++;
            }
        }

        for (nn_chunk *chunk = arena->chunks; chunk != NULL; chunk = chunk->next) {
            stats.chunk_count++;
            stats.mapped_bytes += chunk->map_size;
            stats.heap_bytes += chunk->map_size;

            for (nn_block *block = chunk->first; block != NULL; block = block->next_phys) {
                stats.block_count++;
                if (block->state == NN_BLOCK_USED) {
                    stats.used_bytes += block->requested;
                } else if (block->state == NN_BLOCK_FREE) {
                    stats.free_block_count++;
                    stats.free_bytes += block->size;
                }
            }
        }

        pthread_mutex_unlock(&arena->lock);
    }

    pthread_mutex_unlock(&global_lock);
    return stats;
}

int nn_allocator_check(void)
{
    int ok = 1;

    pthread_mutex_lock(&global_lock);
    for (nn_arena *arena = arenas; arena != NULL && ok; arena = arena->next) {
        if (arena->magic != NN_ARENA_MAGIC) {
            ok = 0;
            break;
        }

        pthread_mutex_lock(&arena->lock);
        for (nn_block *large = arena->large_head; large != NULL; large = large->next_large) {
            if (!block_is_sane(large) || large->state != NN_BLOCK_USED) {
                ok = 0;
                break;
            }
        }

        for (nn_chunk *chunk = arena->chunks; chunk != NULL && ok; chunk = chunk->next) {
            if (chunk->magic != NN_CHUNK_MAGIC) {
                ok = 0;
                break;
            }

            nn_block *prev = NULL;
            for (nn_block *block = chunk->first; block != NULL; block = block->next_phys) {
                if (block->prev_phys != prev || !block_is_sane(block)) {
                    ok = 0;
                    break;
                }
                prev = block;
            }
        }
        pthread_mutex_unlock(&arena->lock);
    }
    pthread_mutex_unlock(&global_lock);

    return ok;
}

size_t nn_allocator_dump_leaks(FILE *out)
{
    size_t leaks = 0;
    FILE *dest = out != NULL ? out : stderr;

    pthread_mutex_lock(&global_lock);
    for (nn_arena *arena = arenas; arena != NULL; arena = arena->next) {
        pthread_mutex_lock(&arena->lock);

        for (nn_block *large = arena->large_head; large != NULL; large = large->next_large) {
            if (large->state == NN_BLOCK_USED) {
                fprintf(dest, "leak #%llu: %zu bytes%s%s%d\n",
                        (unsigned long long)large->alloc_id,
                        large->requested,
                        large->file != NULL ? " at " : "",
                        large->file != NULL ? large->file : "",
                        large->file != NULL ? large->line : 0);
                leaks++;
            }
        }

        for (nn_chunk *chunk = arena->chunks; chunk != NULL; chunk = chunk->next) {
            for (nn_block *block = chunk->first; block != NULL; block = block->next_phys) {
                if (block->state == NN_BLOCK_USED) {
                    fprintf(dest, "leak #%llu: %zu bytes%s%s%d\n",
                            (unsigned long long)block->alloc_id,
                            block->requested,
                            block->file != NULL ? " at " : "",
                            block->file != NULL ? block->file : "",
                            block->file != NULL ? block->line : 0);
                    leaks++;
                }
            }
        }

        pthread_mutex_unlock(&arena->lock);
    }
    pthread_mutex_unlock(&global_lock);

    return leaks;
}
