# nn_alloc

`nn_alloc` is a small C memory allocator inspired by Alejandro Nadal's
["Malloc is not magic"](https://levelup.gitconnected.com/malloc-is-not-magic-implementing-my-own-memory-allocator-e0354e914402)
article.

It follows the same core idea from the article: keep metadata beside each heap
block, track free space in a linked list, split useful chunks, merge freed
neighbors, and grow the heap with `sbrk`.

The implementation adds a few extra pieces:

- aligned payloads for normal C types
- best-fit block selection
- footer checks to catch broken block metadata
- `pthread_mutex_t` locking around allocator state
- page trimming when the last free block gets large
- `calloc`, `realloc`, stats, and heap validation helpers

Public API:

- `nn_malloc`
- `nn_free`
- `nn_calloc`
- `nn_realloc`
- `nn_allocator_stats`

Build and test:

```sh
make test
```

This is still a learning allocator, not a libc replacement. It uses `sbrk`, so
keep it as a project for studying allocator internals instead of linking it into
random production programs.
