# Type-safe, reference-counted pointers to variable-sized classes

[![Build Status](https://travis-ci.com/ppetr/refcounted-var-sized-class.svg?branch=main)](https://travis-ci.com/ppetr/refcounted-var-sized-class)

_*Disclaimer:* This is not an officially supported Google product._

## Summary

- `Ref<T>` is move-only, owns a memory location with an instance of `T`.
  Always contains a value (unless moved out).
- `Ref<const T>` is copy-only, allows only `const` access to an instance of
  `T`. Always contains a value (unless moved out).
- `Ref` is extremely **lightweight**, contain only a single pointer.
- When creating an instance of `T`, additional memory can be
  requested that is passed to the constructor of `T`.
- When creating an instance of `T`, additional memory can be requested that
  is passed to the constructor of `T`.
 - Creating an instance of `T` does a **single memory allocation** (unlike
   `shared_ptr`), even with additional memory.
- `Shared::ToDeleterArg` allows to create a `void*` reference that can be used
  to release memory in old-style C code using `Shared::Deleter`.
- `Ref<T>` can be always converted to `Ref<const T>`.
  The opposite convertion is possible if the caller is a sole owner of it
  (C++17).

## Contributions

Please see [Code of Conduct](docs/code-of-conduct.md) and [Contributing](docs/contributing.md).
