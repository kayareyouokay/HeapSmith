# nn_alloc

An experimental C memory allocator inspired by "Malloc is not magic".

## Build

```sh
make
```

## Test

```sh
make check-all
```

## Try With LD_PRELOAD

```sh
LD_PRELOAD="$PWD/build/libnnalloc.so" ./some_program
```

This is a learning project, not a replacement for production allocators like
glibc `malloc`, jemalloc, mimalloc, or tcmalloc.
