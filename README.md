# Simple and fast user-space [RCU](Read-Copy-Update)[^1] library

[RCU]: https://en.wikipedia.org/wiki/Read-copy-update

[^1]: If you like RCU, you might also like [rCUP](https://circularandco.com/shop/reusables/circular-reusable-coffee-cup) - a reusable coffee cup made from recycled single-use paper cups.

_*Disclaimer:* This is not an officially supported Google product._

[![Build Status](https://app.travis-ci.com/ppetr/lockfree-userspace-rcu.svg?branch=main)](https://app.travis-ci.com/ppetr/lockfree-userspace-rcu)

## Usage

```c++
#include "simple_rcu/rcu.h"

// Shared object that provides an instance of `MyType`:
auto rcu = std::make_shared<simple_rcu::Rcu<MyType>>();

// Any thread can atomically update the value (can be also a `unique_ptr`,
// which auto-converts to `shared_ptr`). This is a relatively slow operation
/ (takes a lock internally).
rcu->Update(std::make_shared<MyType>(...));
```

### Simple

```c++
// Afterwards each reader thread can fetch a const pointer to a snapshot of the
// instance. This is a lock-free operation.
auto ref = simple_rcu::ReadPtr(rcu);
// `ref` now holds a `unique_ptr` to a stable, thread-local snapshot of
// `const MyType`.
```

### Advanced

```c++
// Each reader thread creates a local accessor to `rcu`, which will hold
// snapshots of `MyType`.
simple_rcu::Rcu<MyType>::Local local(rcu);

// Afterwards each reader thread can repeatedly fetch a const pointer to a
// snapshot of the instance. This is faster than the simple usage above, since it
// avoids the internal bookkeeping cost of a `thread_local` variable, at the cost
// of explicitly maintaining a `Local` variable. It effectively involves only a
// single atomic exchange (https://en.cppreference.com/w/cpp/atomic/atomic/exchange)
// instruction.
auto ref = local.ReadPtr();
// `ref` now holds a `unique_ptr` to a stable, thread-local snapshot of
// `const MyType`.
```

See [rcu_test.cc](simple_rcu/rcu_test.cc) for more examples.

## Dependencies

- `cmake` (https://cmake.org/).
- https://github.com/abseil/abseil-cpp (Git submodule)

Testing dependencies:

- https://github.com/google/benchmark (Git submodule)
- https://github.com/google/googletest (Git submodule)

## Benchmarks

The numbers `/1/` or `/4/` after a benchmark name denote the number of
concurrent running threads performing the opposite operation - updates when
benchmarking reads and vice versa.

As shown below, **reads** are very fast and their performance suffers neither
from running concurrent read (1 to 3) nor update (1 or 4) threads. In fact
running multiple read operations benefits from CPU caching.

**Updates** are much slower, as expected. Since (unlike reads) they acquire a
standard [mutex](https://abseil.io/docs/cpp/guides/synchronization), lock
contention occurs when there are multiple concurrent update operations. Also,
when there are multiple concurrent readers, updates become slower, since they
need to distribute values to the readers' thread-local copies.

<dl>
<dt><code>g++</code> on Core i5:</dt>
  <dd>
    <pre>
2023-02-18T22:00:28+01:00
Running ./build/rel-gcc/simple_rcu/copy_rcu_benchmark
Run on (4 X 1948.55 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x4)
  L1 Instruction 32 KiB (x4)
  L2 Unified 256 KiB (x4)
  L3 Unified 6144 KiB (x1)
Load Average: 1.28, 0.51, 0.18
-----------------------------------------------------------------------------------
Benchmark                                         Time             CPU   Iterations
-----------------------------------------------------------------------------------
BM_Reads/1/threads:1                           75.0 ns         75.0 ns      9351178
BM_Reads/1/threads:2                           21.3 ns         42.7 ns     16485720
BM_Reads/1/threads:3                           10.5 ns         31.6 ns     22134405
BM_Reads/4/threads:1                           76.2 ns         76.1 ns     10649990
BM_Reads/4/threads:2                           20.6 ns         41.2 ns     16721828
BM_Reads/4/threads:3                           9.85 ns         28.6 ns     23047518
BM_ReadSharedPtrs/1/threads:1                  73.8 ns         73.8 ns      8486246
BM_ReadSharedPtrs/1/threads:2                  22.7 ns         45.3 ns     16267454
BM_ReadSharedPtrs/1/threads:3                  10.8 ns         32.5 ns     23683905
BM_ReadSharedPtrs/4/threads:1                  58.4 ns         58.3 ns     10000000
BM_ReadSharedPtrs/4/threads:2                  9.79 ns         19.5 ns     33210820
BM_ReadSharedPtrs/4/threads:3                  6.98 ns         20.7 ns     34201104
BM_ReadSharedPtrsThreadLocal/1/threads:1        138 ns          138 ns      4368834
BM_ReadSharedPtrsThreadLocal/1/threads:2       40.8 ns         81.6 ns      7483130
BM_ReadSharedPtrsThreadLocal/1/threads:3       20.8 ns         62.5 ns     11944542
BM_ReadSharedPtrsThreadLocal/4/threads:1        225 ns          224 ns      3341196
BM_ReadSharedPtrsThreadLocal/4/threads:2       59.6 ns          118 ns      5889856
BM_ReadSharedPtrsThreadLocal/4/threads:3       28.7 ns         79.5 ns      9460413
BM_Updates/1/threads:1                          142 ns          142 ns      4936095
BM_Updates/1/threads:2                         41.4 ns         82.8 ns      8346928
BM_Updates/1/threads:3                         20.9 ns         62.7 ns     10951884
BM_Updates/4/threads:1                          429 ns          366 ns      1940278
BM_Updates/4/threads:2                          124 ns          191 ns      3787492
BM_Updates/4/threads:3                         58.7 ns          130 ns      5482329
    </pre>
  </dd>
<dt><code>clang++11</code> on Core i5:</dt>
  <dd>
    <pre>
2023-02-18T22:00:48+01:00
Running ./build/rel-clang11/simple_rcu/copy_rcu_benchmark
Run on (4 X 1672.18 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x4)
  L1 Instruction 32 KiB (x4)
  L2 Unified 256 KiB (x4)
  L3 Unified 6144 KiB (x1)
Load Average: 1.72, 0.65, 0.24
-----------------------------------------------------------------------------------
Benchmark                                         Time             CPU   Iterations
-----------------------------------------------------------------------------------
BM_Reads/1/threads:1                           78.6 ns         78.6 ns      8424422
BM_Reads/1/threads:2                           22.2 ns         44.3 ns     15918648
BM_Reads/1/threads:3                           11.1 ns         33.2 ns     21533877
BM_Reads/4/threads:1                           40.7 ns         40.7 ns     17636729
BM_Reads/4/threads:2                           13.6 ns         27.1 ns     25246078
BM_Reads/4/threads:3                           8.12 ns         21.9 ns     30000000
BM_ReadSharedPtrs/1/threads:1                  66.0 ns         66.0 ns     10045400
BM_ReadSharedPtrs/1/threads:2                  16.7 ns         33.4 ns     15681908
BM_ReadSharedPtrs/1/threads:3                  8.56 ns         25.7 ns     29815704
BM_ReadSharedPtrs/4/threads:1                  56.9 ns         56.9 ns     12850586
BM_ReadSharedPtrs/4/threads:2                  15.5 ns         31.0 ns     19309028
BM_ReadSharedPtrs/4/threads:3                  7.74 ns         23.1 ns     31823679
BM_ReadSharedPtrsThreadLocal/1/threads:1        139 ns          138 ns      4982242
BM_ReadSharedPtrsThreadLocal/1/threads:2       42.6 ns         85.3 ns      8344010
BM_ReadSharedPtrsThreadLocal/1/threads:3       20.6 ns         61.8 ns     10024821
BM_ReadSharedPtrsThreadLocal/4/threads:1        211 ns          211 ns      3380917
BM_ReadSharedPtrsThreadLocal/4/threads:2       63.1 ns          125 ns      6312482
BM_ReadSharedPtrsThreadLocal/4/threads:3       29.1 ns         81.4 ns      9623721
BM_Updates/1/threads:1                          156 ns          156 ns      4479429
BM_Updates/1/threads:2                         39.8 ns         79.6 ns      9281536
BM_Updates/1/threads:3                         20.0 ns         59.9 ns     11436756
BM_Updates/4/threads:1                          428 ns          354 ns      2005830
BM_Updates/4/threads:2                          140 ns          182 ns      3817828
BM_Updates/4/threads:3                         60.2 ns          131 ns      5655744
    </pre>
  </dd>
</dl>

## Further objectives

- Build a lock-free metrics collection library upon it.
- Eventually provide bindings and/or similar implementations in other
  languages: Rust, Haskell, Python, Go, etc.

## Contributions

Please see [Code of Conduct](docs/code-of-conduct.md) and [Contributing](docs/contributing.md).

Ideas for contributions:

- Add bindings/implementations in other languages of your choice.
- Improve documentation where needed. This project should ideally have also
  some didactical value.
- Popularize the project to be useful to others too. In particular there seems
  to be just a copy-left (LGPLv2.1) user-space C++ RCU library
  https://liburcu.org/, so having this one under a more permissive license could
  be attractive for many projects.
