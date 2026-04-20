#include "nn_alloc.h"

#include <assert.h>

int main(void)
{
    assert(nn_allocator_check());
    return 0;
}
