# Type-safe, reference-counted pointers to variable-sized classes

[![Build Status](https://travis-ci.com/ppetr/refcounted-var-sized-class.svg?branch=main)](https://travis-ci.com/ppetr/refcounted-var-sized-class)

_*Disclaimer:* This is not an officially supported Google product._

## Summary

[`MakeUnique(length, args...)`](var_sized.h) creates a new value on the heap
together with an array of size `length` in a **single allocation**. A pointer
to the array is stored in an output argument. Proper destruction is ensured by
a custom deleter.

[`MakeShared(length, args...)`](var_sized.h) creates a new value on the heap
together with an array of size `length` in a **single allocation** (assuming
[`std::allocate_shared`] uses a single allocation). A pointer to the array is
stored in an output argument.  Proper destruction is ensured by a custom
allocator.

[`std::allocate_shared`]: https://en.cppreference.com/w/cpp/memory/shared_ptr/allocate_shared

**Note:** The `Ref` type below, although slightly more performant, is mostly
made obsolete by `MakeShared`.

Type [`Ref`](ref.h) manages a reference-counted value on the heap with
type-safe sharing:

- `Ref<T>` allows mutable access to `T` and is move-only. Always contains a
  value (unless moved out).
- `Ref<const T>` allows `const`-only access to `T`, but can be freely copied.
  Always contains a value (unless moved out).
- `Ref` is very **lightweight**, contain only a single pointer.
- `Ref<T>` can be always converted to `Ref<const T>` by its `Share() &&` method
  and is consumed by the conversion.  The opposite convertion is possible by
  `AttemptToClaim() &&` if the caller is a sole owner of it.

These two concepts can be combined together using `MakeRefCounted`, which
creates a reference-counted, variable-sized structure with a single memory
allocation (akin to [`std::allocate_shared`]).

### Benchmarks

Benchmarks comparing `MakeUnique` and `MakeShared` to the standard `std::`
functions are defined in [var_sized_benchmark.cc](var_sized_benchmark.cc).

Results on _Intel(R) Core(TM) i5-3470 CPU @ 3.20GHz_ with
-DCMAKE_TOOLCHAIN_FILE="devel/toolchain-**clang11**.cmake" -DBENCHMARK_ENABLE_LTO=true -DCMAKE_BUILD_TYPE=Release:

```
CPU Caches:
  L1 Data 32 KiB (x4)
  L1 Instruction 32 KiB (x4)
  L2 Unified 256 KiB (x4)
  L3 Unified 6144 KiB (x1)
----------------------------------------------------------------------------
Benchmark                                  Time             CPU   Iterations
----------------------------------------------------------------------------
BM_VarSizedUniqueString                 1518 ns         1518 ns       462018
BM_VarSizedSharedString                 2567 ns         2559 ns       275427
BM_VarSizedRefCountedString             1521 ns         1520 ns       462505
BM_VarSizedRefCountedSharedString       1553 ns         1553 ns       453952
BM_MakeUniqueStdString                  1745 ns         1745 ns       401544
BM_SharedStdString                      6480 ns         6480 ns       107960
BM_MakeSharedStdString                  5315 ns         5314 ns       130625
```

Results on _Intel(R) Core(TM) i5-3470 CPU @ 3.20GHz_ with
-DCMAKE_TOOLCHAIN_FILE="devel/toolchain-**gcc**.cmake" -DBENCHMARK_ENABLE_LTO=true -DCMAKE_BUILD_TYPE=Release:

```
CPU Caches:
  L1 Data 32 KiB (x4)
  L1 Instruction 32 KiB (x4)
  L2 Unified 256 KiB (x4)
  L3 Unified 6144 KiB (x1)
----------------------------------------------------------------------------
Benchmark                                  Time             CPU   Iterations
----------------------------------------------------------------------------
BM_VarSizedUniqueString                 1517 ns         1517 ns       461315
BM_VarSizedSharedString                 2552 ns         2552 ns       275468
BM_VarSizedRefCountedString             1572 ns         1572 ns       446478
BM_VarSizedRefCountedSharedString       1574 ns         1574 ns       444534
BM_MakeUniqueStdString                  3087 ns         3087 ns       227068
BM_SharedStdString                      5262 ns         5262 ns       132952
BM_MakeSharedStdString                  3947 ns         3947 ns       177140
```

### Copy-on-Write

[`CopyOnWrite`](copy_on_write.h) is an experimental type that manages an
instance of `T` on the heap. Copying `CopyOnWrite<T>` is as cheap as copying a
pointer, since copies are allowed to share a single instance. An actual copy of
`T` is performed only when a mutable reference `T&` is requested.

## Dependencies

- `cmake` (https://cmake.org/).
- https://github.com/abseil/abseil-cpp (Git submodule)

Testing dependencies:

- https://github.com/google/benchmark (Git submodule)
- https://github.com/google/googletest (Git submodule)

## Contributions

Please see [Code of Conduct](docs/code-of-conduct.md) and [Contributing](docs/contributing.md).

Ideas for contributions:

- Add benchmarks with https://github.com/google/benchmark. In particular:
  - Test the creation and manipulation speed of `Ref<T>` vs
    `std::unique_ptr<T>` and `Ref<const T>` vs `std::shared_ptr<T>`.
  - Test the allocation speed of variable-sized classes vs a regular class and
    a separately allocated array.
- Allow arbitrary C++
  [allocators](https://en.cppreference.com/w/cpp/named_req/Allocator).
- Improve documentation where needed. This project should ideally have also
  some didactical value.
- Popularize the project to be useful to others too.
