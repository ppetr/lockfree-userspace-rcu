# Simple and fast user-space [RCU](Read-Copy-Update)[^1] library

[RCU]: https://en.wikipedia.org/wiki/Read-copy-update

[^1]: If you like RCU, you might also like [rCUP](https://circularandco.com/shop/reusables/circular-reusable-coffee-cup) - a reusable coffee cup made from recycled single-use paper cups.

_*Disclaimer:* This is not an officially supported Google product._

## Usage

```c++
#include "simple_rcu/rcu.h"

// Shared object that provides an instance of `MyType`:
Rcu<MyType> rcu;

// Each reader thread creates a local accessor to `rcu` (this can be simplified
// using a `thread_local` variable).
Rcu<MyType>::Local local(rcu);

// Any thread can atomically update the value (can be also a `unique_ptr`):
rcu.Update(std::make_shared<MyType>(...));

// Afterwards each reader thread can fetch a const pointer to a snapshot of the
// instance:
local.ReadPtr()->ConstMethodOnMyType(...);
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
2023-01-09T11:20:47+01:00
Running build/rel-gcc/simple_rcu/copy_rcu_benchmark
Run on (4 X 1981.52 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x4)
  L1 Instruction 32 KiB (x4)
  L2 Unified 256 KiB (x4)
  L3 Unified 6144 KiB (x1)
Load Average: 0.40, 0.13, 0.12
------------------------------------------------------------------------
Benchmark                              Time             CPU   Iterations
------------------------------------------------------------------------
BM_Reads/1/threads:1                59.3 ns         59.3 ns     11819321
BM_Reads/1/threads:2                21.4 ns         42.7 ns     16274508
BM_Reads/1/threads:3                10.3 ns         31.0 ns     20191095
BM_Reads/4/threads:1                55.7 ns         55.7 ns     12663755
BM_Reads/4/threads:2                10.1 ns         20.1 ns     38452336
BM_Reads/4/threads:3                9.47 ns         26.8 ns     38557011
BM_ReadSharedPtrs/1/threads:1       66.9 ns         66.9 ns     10383232
BM_ReadSharedPtrs/1/threads:2       12.9 ns         25.7 ns     20000000
BM_ReadSharedPtrs/1/threads:3       4.91 ns         14.7 ns     42913059
BM_ReadSharedPtrs/4/threads:1       35.9 ns         35.9 ns     23200403
BM_ReadSharedPtrs/4/threads:2       5.03 ns         10.1 ns     63553140
BM_ReadSharedPtrs/4/threads:3       4.67 ns         13.5 ns     56092143
BM_Updates/1/threads:1               128 ns          128 ns      5502765
BM_Updates/1/threads:2               203 ns          402 ns      1694968
BM_Updates/1/threads:3               189 ns          479 ns      1495890
BM_Updates/4/threads:1               435 ns          374 ns      1827026
BM_Updates/4/threads:2               406 ns          384 ns      1986608
BM_Updates/4/threads:3               386 ns          494 ns      1299039
    </pre>
  </dd>
<dt><code>clang++11</code> on Core i5:</dt>
  <dd>
    <pre>
2023-01-09T11:20:26+01:00
Running build/rel-clang11/simple_rcu/copy_rcu_benchmark
Run on (4 X 2180.18 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x4)
  L1 Instruction 32 KiB (x4)
  L2 Unified 256 KiB (x4)
  L3 Unified 6144 KiB (x1)
Load Average: 0.11, 0.05, 0.10
------------------------------------------------------------------------
Benchmark                              Time             CPU   Iterations
------------------------------------------------------------------------
BM_Reads/1/threads:1                77.9 ns         77.9 ns      8973229
BM_Reads/1/threads:2                16.8 ns         33.5 ns     19910656
BM_Reads/1/threads:3                8.55 ns         25.7 ns     27743406
BM_Reads/4/threads:1                67.8 ns         67.7 ns     10000000
BM_Reads/4/threads:2                8.30 ns         16.5 ns     44192786
BM_Reads/4/threads:3                7.20 ns         20.1 ns     30000000
BM_ReadSharedPtrs/1/threads:1       52.5 ns         52.5 ns     13525121
BM_ReadSharedPtrs/1/threads:2       8.99 ns         18.0 ns     42398912
BM_ReadSharedPtrs/1/threads:3       5.00 ns         15.0 ns     44616918
BM_ReadSharedPtrs/4/threads:1       46.5 ns         46.5 ns     13235953
BM_ReadSharedPtrs/4/threads:2       4.90 ns         9.74 ns     58649326
BM_ReadSharedPtrs/4/threads:3       5.09 ns         14.6 ns     47492265
BM_Updates/1/threads:1               146 ns          146 ns      4773062
BM_Updates/1/threads:2               172 ns          344 ns      1867244
BM_Updates/1/threads:3               172 ns          515 ns      1238121
BM_Updates/4/threads:1               533 ns          388 ns      1775958
BM_Updates/4/threads:2               571 ns          440 ns      1524154
BM_Updates/4/threads:3               358 ns          510 ns      1448880
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
