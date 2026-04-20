# nn_alloc

`nn_alloc` is a small C memory allocator inspired by Alejandro Nadal's
"Malloc is not magic" article.

It uses a linked list of heap blocks, grows the heap with `sbrk`, and exposes a
simple API:

- `nn_malloc`
- `nn_free`
- `nn_calloc`
- `nn_realloc`
- `nn_allocator_stats`

Build and test:

```sh
make test
```

This is a learning allocator, not a libc replacement.
