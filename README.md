# Simple and fast user-space [RCU](Read-Copy-Update)[^1] and metric collection library

[RCU]: https://en.wikipedia.org/wiki/Read-copy-update

[^1]: If you like RCU, you might also like [rCUP](https://circularandco.com/shop/reusables/circular-reusable-coffee-cup) - a reusable coffee cup made from recycled single-use paper cups.

_*Disclaimer:* This is not an officially supported Google product._

[![Build Status](https://app.travis-ci.com/ppetr/lockfree-userspace-rcu.svg?branch=main)](https://app.travis-ci.com/ppetr/lockfree-userspace-rcu)

This library builds on (possibly) novel concepts that allows critical
operations (fetching a RCU snapshot, updating a value in a metric) to be
atomic, lock-free and
**[wait-free](https://en.wikipedia.org/wiki/Non-blocking_algorithm#Wait-freedom)**
(assuming that access to a `thread_local` variable can be considered wait-free).

## Usage

### RCU

```c++
#include "simple_rcu/rcu.h"

// A central object that distributes instances of `MyType`:
auto rcu = simple_rcu::Rcu<MyType>();

// Any thread can atomically update the value (can be also a `unique_ptr`,
// which auto-converts to `shared_ptr`). This is a relatively slow operation
// (takes a lock internally).
rcu.Update(std::make_shared<MyType>(...));

// Fetch the most recently `Update`d value. This is a wait-free operation.
// See benchmark `BM_SnapshotSharedPtrThreadLocal` below.
std::shared_ptr<MyType> ptr = simple_rcu::Snapshot(rcu);
```

### Metrics

```c++
#include "simple_rcu/lock_free_metric.h"

// A central object that collects values of a given type.
simple_rcu::LockFreeMetric<absl::int128> metric;

// Any thread can increment the metric using a very fast, wait-free `Update`
// operation:
metric.Update(42);
metric.Update(73);

// Some other thread can collect the metrics accumulated by all threads that
// called `Update` before. This also resets the thread-local numbers back to 0.
std::vector<absl::int128> collected = metric.Collect();
// `collected` will now contain a single value of 42+73.
```

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
2023-03-08T19:37:57+01:00
Running build/rel-gcc/simple_rcu/copy_rcu_benchmark
Run on (4 X 3572.69 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x4)
  L1 Instruction 32 KiB (x4)
  L2 Unified 256 KiB (x4)
  L3 Unified 6144 KiB (x1)
Load Average: 0.33, 1.50, 1.26
------------------------------------------------------------------------------------
Benchmark                                          Time             CPU   Iterations
------------------------------------------------------------------------------------
BM_Reads/1/threads:1                            88.0 ns         88.0 ns      7925289
BM_Reads/1/threads:2                            17.7 ns         35.4 ns     21247450
BM_Reads/1/threads:4                            6.64 ns         22.5 ns     35341128
BM_Reads/1/threads:8                            2.30 ns         10.1 ns     61592216
BM_Reads/1/threads:16                           1.58 ns         7.43 ns     87989696
BM_Reads/1/threads:32                           1.27 ns         6.74 ns     94589952
BM_Reads/1/threads:64                          0.841 ns         6.64 ns    106161024
BM_Reads/4/threads:1                            84.3 ns         84.3 ns     11161830
BM_Reads/4/threads:2                            6.84 ns         13.7 ns     39595196
BM_Reads/4/threads:4                            4.89 ns         15.8 ns     55958100
BM_Reads/4/threads:8                            2.11 ns         8.99 ns     69275648
BM_Reads/4/threads:16                           1.59 ns         7.79 ns     67754640
BM_Reads/4/threads:32                           1.49 ns         7.00 ns     98964928
BM_Reads/4/threads:64                          0.735 ns         6.54 ns    103060736
BM_ReadSharedPtrs/1/threads:1                   62.9 ns         62.9 ns     11343017
BM_ReadSharedPtrs/1/threads:2                   12.4 ns         24.9 ns     28810718
BM_ReadSharedPtrs/1/threads:4                   4.03 ns         13.6 ns     47565036
BM_ReadSharedPtrs/1/threads:8                   2.02 ns         8.41 ns     73046840
BM_ReadSharedPtrs/1/threads:16                  1.62 ns         7.22 ns     96594224
BM_ReadSharedPtrs/1/threads:32                  1.37 ns         6.76 ns     98965440
BM_ReadSharedPtrs/1/threads:64                 0.925 ns         6.57 ns    102769984
BM_ReadSharedPtrs/4/threads:1                   50.0 ns         50.0 ns     10000000
BM_ReadSharedPtrs/4/threads:2                   5.64 ns         11.2 ns     56381504
BM_ReadSharedPtrs/4/threads:4                   3.83 ns         11.2 ns     45073088
BM_ReadSharedPtrs/4/threads:8                   2.17 ns         8.11 ns     78239648
BM_ReadSharedPtrs/4/threads:16                  1.72 ns         7.28 ns     89113344
BM_ReadSharedPtrs/4/threads:32                  1.46 ns         6.82 ns     99200224
BM_ReadSharedPtrs/4/threads:64                  1.02 ns         6.52 ns    104567680
BM_ReadSharedPtrsThreadLocal/1/threads:1         101 ns          101 ns      6553118
BM_ReadSharedPtrsThreadLocal/1/threads:2        51.5 ns          103 ns      6001084
BM_ReadSharedPtrsThreadLocal/1/threads:4        41.2 ns          134 ns      5285084
BM_ReadSharedPtrsThreadLocal/1/threads:8        35.4 ns          138 ns      5366944
BM_ReadSharedPtrsThreadLocal/1/threads:16       26.3 ns          128 ns      5000608
BM_ReadSharedPtrsThreadLocal/1/threads:32       28.3 ns          142 ns      5779744
BM_ReadSharedPtrsThreadLocal/1/threads:64       19.2 ns          126 ns      5579776
BM_ReadSharedPtrsThreadLocal/4/threads:1         113 ns          113 ns      5932706
BM_ReadSharedPtrsThreadLocal/4/threads:2        64.4 ns          129 ns      5636694
BM_ReadSharedPtrsThreadLocal/4/threads:4        45.8 ns          129 ns      4000000
BM_ReadSharedPtrsThreadLocal/4/threads:8        34.4 ns          127 ns      5632464
BM_ReadSharedPtrsThreadLocal/4/threads:16       28.4 ns          121 ns      4951344
BM_ReadSharedPtrsThreadLocal/4/threads:32       33.7 ns          139 ns      5735200
BM_ReadSharedPtrsThreadLocal/4/threads:64       25.9 ns          137 ns      6150656
BM_Updates/1/threads:1                           126 ns          126 ns      5545689
BM_Updates/1/threads:2                           136 ns          271 ns      2289246
BM_Updates/1/threads:4                           133 ns          320 ns      1883372
BM_Updates/1/threads:8                           114 ns          145 ns      3911272
BM_Updates/1/threads:16                         98.1 ns          138 ns      5120464
BM_Updates/1/threads:32                         92.4 ns          146 ns      3935488
BM_Updates/1/threads:64                         73.1 ns          141 ns      4169792
BM_Updates/4/threads:1                           394 ns          310 ns      2319023
BM_Updates/4/threads:2                           320 ns          403 ns      1572682
BM_Updates/4/threads:4                           347 ns          460 ns      2120880
BM_Updates/4/threads:8                           317 ns          337 ns      2066568
BM_Updates/4/threads:16                          279 ns          341 ns      1708928
BM_Updates/4/threads:32                          265 ns          330 ns      2127232
BM_Updates/4/threads:64                          233 ns          321 ns      2190336
    </pre>
  </dd>
<dt><code>clang++11</code> on Core i5:</dt>
  <dd>
    <pre>
2023-03-08T19:38:30+01:00
Running build/rel-clang11/simple_rcu/copy_rcu_benchmark
Run on (4 X 1694.88 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x4)
  L1 Instruction 32 KiB (x4)
  L2 Unified 256 KiB (x4)
  L3 Unified 6144 KiB (x1)
Load Average: 4.90, 2.46, 1.58
------------------------------------------------------------------------------------
Benchmark                                          Time             CPU   Iterations
------------------------------------------------------------------------------------
BM_Reads/1/threads:1                            87.7 ns         87.7 ns      7955889
BM_Reads/1/threads:2                            15.3 ns         30.6 ns     23136186
BM_Reads/1/threads:4                            7.42 ns         25.2 ns     28852784
BM_Reads/1/threads:8                            2.81 ns         12.1 ns     48782816
BM_Reads/1/threads:16                           1.43 ns         7.65 ns     70177200
BM_Reads/1/threads:32                           1.33 ns         6.80 ns     97955488
BM_Reads/1/threads:64                          0.952 ns         6.61 ns     98974848
BM_Reads/4/threads:1                            58.5 ns         58.5 ns      9621076
BM_Reads/4/threads:2                            8.63 ns         17.2 ns     39341958
BM_Reads/4/threads:4                            6.68 ns         20.9 ns     49974640
BM_Reads/4/threads:8                            4.36 ns         15.6 ns     46642208
BM_Reads/4/threads:16                           1.78 ns         8.27 ns     68227440
BM_Reads/4/threads:32                           1.35 ns         7.09 ns     94126560
BM_Reads/4/threads:64                          0.876 ns         6.91 ns     99590016
BM_ReadSharedPtrs/1/threads:1                   63.4 ns         63.4 ns     11204514
BM_ReadSharedPtrs/1/threads:2                   12.6 ns         25.2 ns     20000000
BM_ReadSharedPtrs/1/threads:4                   5.01 ns         16.4 ns     38396812
BM_ReadSharedPtrs/1/threads:8                   2.87 ns         11.9 ns     61796344
BM_ReadSharedPtrs/1/threads:16                  2.02 ns         9.59 ns     78273408
BM_ReadSharedPtrs/1/threads:32                  1.48 ns         7.91 ns     80529600
BM_ReadSharedPtrs/1/threads:64                  1.25 ns         10.6 ns     56653504
BM_ReadSharedPtrs/4/threads:1                   63.8 ns         63.8 ns     10000000
BM_ReadSharedPtrs/4/threads:2                   5.83 ns         11.6 ns     68954404
BM_ReadSharedPtrs/4/threads:4                   4.44 ns         13.9 ns     58391728
BM_ReadSharedPtrs/4/threads:8                   2.75 ns         11.3 ns     53274936
BM_ReadSharedPtrs/4/threads:16                  2.66 ns         10.8 ns     93864160
BM_ReadSharedPtrs/4/threads:32                  2.44 ns         10.4 ns     64900896
BM_ReadSharedPtrs/4/threads:64                 0.823 ns         7.14 ns     77218560
BM_ReadSharedPtrsThreadLocal/1/threads:1         134 ns          134 ns      5011638
BM_ReadSharedPtrsThreadLocal/1/threads:2        70.4 ns          141 ns      6701984
BM_ReadSharedPtrsThreadLocal/1/threads:4        43.2 ns          136 ns      4510720
BM_ReadSharedPtrsThreadLocal/1/threads:8        41.4 ns          160 ns      5049376
BM_ReadSharedPtrsThreadLocal/1/threads:16       35.2 ns          146 ns      4635376
BM_ReadSharedPtrsThreadLocal/1/threads:32       32.4 ns          149 ns      4752544
BM_ReadSharedPtrsThreadLocal/1/threads:64       18.8 ns          146 ns      5139008
BM_ReadSharedPtrsThreadLocal/4/threads:1         156 ns          156 ns      4373200
BM_ReadSharedPtrsThreadLocal/4/threads:2        84.6 ns          159 ns      4440214
BM_ReadSharedPtrsThreadLocal/4/threads:4        54.2 ns          155 ns      4642584
BM_ReadSharedPtrsThreadLocal/4/threads:8        44.2 ns          147 ns      4755640
BM_ReadSharedPtrsThreadLocal/4/threads:16       34.9 ns          147 ns      5078864
BM_ReadSharedPtrsThreadLocal/4/threads:32       37.0 ns          152 ns      4980928
BM_ReadSharedPtrsThreadLocal/4/threads:64       23.4 ns          149 ns      4204736
BM_Updates/1/threads:1                           129 ns          129 ns      5429974
BM_Updates/1/threads:2                           205 ns          410 ns      1625164
BM_Updates/1/threads:4                           134 ns          155 ns      3337368
BM_Updates/1/threads:8                           112 ns          138 ns      3871216
BM_Updates/1/threads:16                         96.5 ns          142 ns      4074256
BM_Updates/1/threads:32                         96.9 ns          142 ns      3858176
BM_Updates/1/threads:64                         75.3 ns          147 ns      4228096
BM_Updates/4/threads:1                           422 ns          339 ns      2059096
BM_Updates/4/threads:2                           464 ns          412 ns      1585970
BM_Updates/4/threads:4                           412 ns          419 ns      1312416
BM_Updates/4/threads:8                           335 ns          348 ns      1878216
BM_Updates/4/threads:16                          318 ns          342 ns      1556608
BM_Updates/4/threads:32                          243 ns          331 ns      1952704
BM_Updates/4/threads:64                          257 ns          355 ns      2050752
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
