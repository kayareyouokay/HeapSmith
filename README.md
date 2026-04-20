# nn_alloc

`nn_alloc` is a small C memory allocator inspired by Alejandro Nadal's
["Malloc is not magic"](https://levelup.gitconnected.com/malloc-is-not-magic-implementing-my-own-memory-allocator-e0354e914402)
article.

It started from the same core idea in the article: keep metadata beside each
heap block, track free space, split useful chunks, and merge freed neighbors.
The current version goes further and uses `mmap` arenas instead of `sbrk`.

The implementation now includes:

- per-thread arenas to reduce lock contention
- segregated free-list bins for small and medium allocations
- direct `mmap` mappings for large and explicitly aligned allocations
- aligned payloads for normal C types
- block splitting and neighbor coalescing
- canary checks before and after payload storage
- freed-memory poisoning
- optional debug quarantine with `-DNN_ALLOC_DEBUG`
- `calloc`, `realloc`, `reallocarray`, `posix_memalign`, and `aligned_alloc`
- leak dumping, stats, usable-size, and heap validation helpers
- a shared library that can be tested with `LD_PRELOAD`
- sanitizer, preload, fuzz, long-fuzz, and benchmark targets

Public API:

- `nn_malloc`
- `nn_free`
- `nn_calloc`
- `nn_realloc`
- `nn_reallocarray`
- `nn_posix_memalign`
- `nn_aligned_alloc`
- `nn_malloc_usable_size`
- `nn_allocator_dump_leaks`
- `nn_get_allocator_stats`
- `nn_allocator_check`

Build:

```sh
make
make test
make debug-test
make sanitize-test
make preload-test
make hostile-preload-test
make fuzz
make long-fuzz
make sanitize-fuzz
make bench
make bench-system
```

Full local verification:

```sh
make check-all
```

`long-fuzz` defaults to 250,000 randomized operations. You can raise it:

```sh
make long-fuzz LONG_FUZZ_STEPS=2000000
```

Try the preload build:

```sh
LD_PRELOAD="$PWD/build/libnnalloc.so" ./some_program
```

This is much closer to a real allocator than the first version, but it still
needs more workload benchmarks, broader platform testing, and real application
preload trials before anyone should trust it like jemalloc, mimalloc, tcmalloc,
or glibc `malloc`.

The benchmark can also be run against another allocator with `LD_PRELOAD`:

```sh
make bench-system
LD_PRELOAD=/path/to/libjemalloc.so ./build/bench_system
```
