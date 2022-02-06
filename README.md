# Simple and fast user-space [RCU](Read-Copy-Update)[^1] library

[RCU]: https://en.wikipedia.org/wiki/Read-copy-update

[^1]: If you like RCU, you might also like [rCUP](https://circularandco.com/shop/reusables/circular-reusable-coffee-cup) - a reusable coffee cup made from recycled single-use paper cups.

_*Disclaimer:* This is not an officially supported Google product._

## Usage

```c++
#include "simple_rcu/rcu.h"

// Shared object that provides an instance of copyable `MyType`:
Rcu<MyType> rcu;

// Each reader thread creates a local accessor to `rcu`:
Rcu<int>::Local local(rcu);
// Any thread can atomically update the value:
rcu.Update(MyType(...));
// Afterwards a reader thread can fetch a reference to its new, updated copy:
local.Read()->MethodOnMyType(...);
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
Run on (4 X 2071.44 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x4)
  L1 Instruction 32 KiB (x4)
  L2 Unified 256 KiB (x4)
  L3 Unified 6144 KiB (x1)
-----------------------------------------------------------------
Benchmark                       Time             CPU   Iterations
-----------------------------------------------------------------
BM_Reads/1/threads:1         75.1 ns         75.1 ns      9530103
BM_Reads/1/threads:2         18.4 ns         36.9 ns     19012958
BM_Reads/1/threads:3         9.37 ns         28.1 ns     24518454
BM_Reads/4/threads:1         58.6 ns         58.5 ns     11649360
BM_Reads/4/threads:2         8.65 ns         17.3 ns     32108164
BM_Reads/4/threads:3         10.6 ns         29.8 ns     30000000
BM_Updates/1/threads:1        125 ns          125 ns      5614515
BM_Updates/1/threads:2        154 ns          308 ns      2347596
BM_Updates/1/threads:3        200 ns          593 ns      1209459
BM_Updates/4/threads:1        495 ns          371 ns      1933854
BM_Updates/4/threads:2        338 ns          359 ns      1461652
BM_Updates/4/threads:3        420 ns          522 ns      1469535
</pre>
</dd>
<dt><code>clang++11</code> on Core i5:</dt>
<dd>
<pre>
Run on (4 X 1861.48 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x4)
  L1 Instruction 32 KiB (x4)
  L2 Unified 256 KiB (x4)
  L3 Unified 6144 KiB (x1)
-----------------------------------------------------------------
Benchmark                       Time             CPU   Iterations
-----------------------------------------------------------------
BM_Reads/1/threads:1         94.3 ns         94.3 ns      7396679
BM_Reads/1/threads:2         27.9 ns         55.7 ns     12433586
BM_Reads/1/threads:3         13.8 ns         41.4 ns     16416459
BM_Reads/4/threads:1         64.7 ns         64.7 ns     11788066
BM_Reads/4/threads:2         13.5 ns         26.9 ns     26770212
BM_Reads/4/threads:3         13.0 ns         36.3 ns     17137263
BM_Updates/1/threads:1        110 ns          110 ns      6322367
BM_Updates/1/threads:2        165 ns          329 ns      1996366
BM_Updates/1/threads:3        193 ns          555 ns      1128912
BM_Updates/4/threads:1        480 ns          365 ns      1934744
BM_Updates/4/threads:2        390 ns          381 ns      1439624
BM_Updates/4/threads:3        429 ns          491 ns      1630869
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

- Add benchmarks.
- Add bindings/implementations in other languages of your choice.
- Improve documentation where needed. This project should ideally have also
  some didactical value.
- Popularize the project to be useful to others too. In particular there seems
  to be just a copy-left (LGPLv2.1) user-space C++ RCU library
  https://liburcu.org/, so having this one under a more permissive license could
  be attractive for many projects.
