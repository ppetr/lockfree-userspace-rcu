# Simple and fast user-space RCU library

_*Disclaimer:* This is not an officially supported Google product._

## Objectives

- Create a simple and fast user-space C++
[RCU](https://en.wikipedia.org/wiki/Read-copy-update) (Read-Copy-Update)
library.
- Build a lock-free metrics collection library upon it.
- Eventually provide bindings and/or similar implementations in other
  languages: Rust, Haskell, Python, Go, etc.

## Dependencies

- `cmake` (https://cmake.org/).
- https://github.com/abseil/abseil-cpp (Git submodule)

Testing dependencies:

- https://github.com/google/benchmark (Git submodule)
- https://github.com/google/googletest (Git submodule)

## Contributions

Please see [Code of Conduct](docs/code-of-conduct.md) and [Contributing](docs/contributing.md).

Ideas for contributions:

- Add benchmarks.
- Add bindings/implementations in other languages of your choice.
- Improve documentation where needed. This project should ideally have also
  some didactical value.
- Popularize the project to be useful to others too. In particular there seems
  to be just a copy-left (LGPLv2.1) user-space C++ RCU library
  https://liburcu.org/, so having this one under a more permissive license could
  be attractive for many projects.
