#include "nn_alloc.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define SLOTS 512
#define STEPS 50000

typedef struct slot {
    unsigned char *ptr;
    size_t size;
    unsigned char value;
} slot;

static uint32_t next_rand(uint32_t *state)
{
    *state = (*state * 1664525U) + 1013904223U;
    return *state;
}

static void check_slot(slot *item)
{
    if (item->ptr == NULL) {
        return;
    }

    for (size_t i = 0; i < item->size; i++) {
        assert(item->ptr[i] == item->value);
    }
}

int main(void)
{
    slot slots[SLOTS] = {0};
    uint32_t state = 0x12345678U;

    for (size_t step = 0; step < STEPS; step++) {
        size_t index = next_rand(&state) % SLOTS;
        size_t action = next_rand(&state) % 4;
        size_t size = (next_rand(&state) % 8192U) + 1U;

        check_slot(&slots[index]);

        if (action == 0) {
            nn_free(slots[index].ptr);
            slots[index] = (slot){0};
        } else if (action == 1 && slots[index].ptr != NULL) {
            unsigned char *new_ptr = nn_realloc(slots[index].ptr, size);
            assert(new_ptr != NULL);
            slots[index].ptr = new_ptr;
            if (size > slots[index].size) {
                memset(slots[index].ptr + slots[index].size,
                       slots[index].value,
                       size - slots[index].size);
            }
            slots[index].size = size;
        } else {
            nn_free(slots[index].ptr);
            slots[index].ptr = nn_malloc(size);
            assert(slots[index].ptr != NULL);
            slots[index].size = size;
            slots[index].value = (unsigned char)(next_rand(&state) & 0xff);
            memset(slots[index].ptr, slots[index].value, size);
        }

        assert(nn_allocator_check());
    }

    for (size_t i = 0; i < SLOTS; i++) {
        check_slot(&slots[i]);
        nn_free(slots[i].ptr);
    }

    return nn_allocator_check() ? 0 : 1;
}
