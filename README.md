# Type-safe, reference-counted pointers to variable-sized classes

[![Build Status](https://travis-ci.com/ppetr/refcounted-var-sized-class.svg?branch=main)](https://travis-ci.com/ppetr/refcounted-var-sized-class)

_*Disclaimer:* This is not an officially supported Google product._

## Summary

[`MakeUnique(length, args...)`](var_sized.h) creates a new value on the heap
together with an array of size `length` in a **single allocation**.  The array
is passed as an additional argument when constructing `T`. Proper destruction
is ensured by a custom deleter.

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

These two concepts can be combined together using `MakeRefCounter`, which
creates a reference-counted, variable-sized structure with a single memory
allocation (unlike `std::shared_ptr`, which needs to allocate storage for its
bookkeeping separately)

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

- Refactor and improve tests with https://github.com/google/googletest.
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
