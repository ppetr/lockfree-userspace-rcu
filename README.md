# Type-safe, reference-counted pointers to variable-sized classes

_*Disclaimer:* This is not an officially supported Google product._

## Summary

- `Unique<T>` is move-only, owns a memory location with an instance of `T`.
  Always contains a value (unless moved out).
- `Shared<T>` is copy-only, allows only `const` access to an instance of `T`.
  Always contains a value (unless moved out).
- `Unique<T>` can be always converted to `Shared<T>`.
  The opposite convertion is possible if the caller is a sole owner of it.
- Both `Shared` and `Unique` are extremely **lightweight**, contain only a
  single pointer.
- When creating an instance of `T`, an **additional block of memory** can be
  requested that is passed to the constructor of `T`.
- Creating an instance of `T` does a **single memory allocation** (unlike
  `shared_ptr`), even with an additional memory block.
- `Shared::ToDeleterArg` allows to create a `void*` reference that can be used
  to release memory in old-style C code using `static Shared::Deleter`.

## Contributions

Please see [Code of Conduct](docs/code-of-conduct.md) and [Contributing](docs/contributing.md).
