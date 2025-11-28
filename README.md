# Simple and fast user-space [RCU](Read-Copy-Update)[^1] and metric collection library

[RCU]: https://en.wikipedia.org/wiki/Read-copy-update

[^1]: If you like RCU, you might also like [rCUP](https://circularandco.com/shop/reusables/circular-reusable-coffee-cup) - a reusable coffee cup made from recycled single-use paper cups.

_*Disclaimer:* This is not an officially supported Google product._

[![Build Status](https://app.travis-ci.com/ppetr/lockfree-userspace-rcu.svg?branch=main)](https://app.travis-ci.com/ppetr/lockfree-userspace-rcu)

This library builds on (possibly) novel concepts that allows critical
operations (fetching a RCU snapshot, updating a value in a metric) to be
atomic, lock-free and
**[wait-free](https://en.wikipedia.org/wiki/Non-blocking_algorithm#Wait-freedom)**.

## Design principles

- Critical operations (RCU reading a snapshot, updating a metric) are lock- and
  wait-free.
- Instant data propagation. An operation finished by one thread should be
  immediately visible to the other involved threads. In particular:
  - RCU - once a call to `Update` finishes, all threads will observe the new
    version in their `Snapshot`.
  - Metric - when `Collect` is called, it observes all finished past calls to
    `Update`.
- Simple - internal logic should be encapsulated.

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
std::shared_ptr<MyType> ptr = rcu.Snapshot();
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

### RCU

The numbers `/1/` or `/4/` after a benchmark name denote the number of
concurrent running threads performing the opposite operation - updates when
benchmarking reads and vice versa.

As shown below, **reads** are very fast and their performance suffers neither
from running concurrent read (1 to 3) nor update (1 or 4) threads.

**Updates** are much slower, as expected. Since (unlike reads) they acquire a
standard [mutex](https://abseil.io/docs/cpp/guides/synchronization), lock
contention occurs when there are multiple concurrent update operations. Also,
when there are multiple concurrent readers, updates become slower, since they
need to distribute values to the readers' thread-local copies.

| header | value |
| --- | --- |
| executable | ./build/rel-clang/simple_rcu/copy_rcu_benchmark |
| num_cpus | 4 |
| mhz_per_cpu | 3524 |
| library_build_type | release |

#### BM\_Snapshot

| name | iterations | threads | cpu\_time | real\_time |
| :- | -: | -: | -: | -: |
| BM\_Snapshot/1/threads:1 | 13518688 | 1 | 43 ns | 43 ns |
| BM\_Snapshot/1/threads:2 | 23750486 | 2 | 27 ns | 13 ns |
| BM\_Snapshot/1/threads:4 | 40000000 | 4 | 17 ns | 5 ns |
| BM\_Snapshot/1/threads:8 | 96646584 | 8 | 8 ns | 2 ns |
| BM\_Snapshot/1/threads:16 | 101540512 | 16 | 7 ns | 2 ns |
| BM\_Snapshot/1/threads:32 | 104732192 | 32 | 7 ns | 1 ns |
| BM\_Snapshot/1/threads:64 | 102920576 | 64 | 7 ns | 1 ns |
| BM\_Snapshot/4/threads:1 | 26839232 | 1 | 26 ns | 26 ns |
| BM\_Snapshot/4/threads:2 | 50143254 | 2 | 20 ns | 10 ns |
| BM\_Snapshot/4/threads:4 | 70202392 | 4 | 10 ns | 3 ns |
| BM\_Snapshot/4/threads:8 | 70626424 | 8 | 9 ns | 2 ns |
| BM\_Snapshot/4/threads:16 | 81104384 | 16 | 7 ns | 2 ns |
| BM\_Snapshot/4/threads:32 | 103353824 | 32 | 7 ns | 1 ns |
| BM\_Snapshot/4/threads:64 | 105050816 | 64 | 7 ns | 1 ns |

#### BM\_SnapshotThreadLocal

| name | iterations | threads | cpu\_time | real\_time |
| :- | -: | -: | -: | -: |
| BM\_SnapshotThreadLocal/1/threads:1 | 11851888 | 1 | 81 ns | 81 ns |
| BM\_SnapshotThreadLocal/1/threads:2 | 14720876 | 2 | 48 ns | 24 ns |
| BM\_SnapshotThreadLocal/1/threads:4 | 39241168 | 4 | 28 ns | 9 ns |
| BM\_SnapshotThreadLocal/1/threads:8 | 50072920 | 8 | 12 ns | 4 ns |
| BM\_SnapshotThreadLocal/1/threads:16 | 63454560 | 16 | 11 ns | 2 ns |
| BM\_SnapshotThreadLocal/1/threads:32 | 50231392 | 32 | 12 ns | 2 ns |
| BM\_SnapshotThreadLocal/1/threads:64 | 48901632 | 64 | 12 ns | 2 ns |
| BM\_SnapshotThreadLocal/4/threads:1 | 13465170 | 1 | 61 ns | 61 ns |
| BM\_SnapshotThreadLocal/4/threads:2 | 17475556 | 2 | 36 ns | 18 ns |
| BM\_SnapshotThreadLocal/4/threads:4 | 33243928 | 4 | 17 ns | 5 ns |
| BM\_SnapshotThreadLocal/4/threads:8 | 46148576 | 8 | 13 ns | 3 ns |
| BM\_SnapshotThreadLocal/4/threads:16 | 59590368 | 16 | 11 ns | 2 ns |
| BM\_SnapshotThreadLocal/4/threads:32 | 41398080 | 32 | 13 ns | 3 ns |
| BM\_SnapshotThreadLocal/4/threads:64 | 47122816 | 64 | 13 ns | 3 ns |

#### BM\_SnapshotSharedPtr

| name | iterations | threads | cpu\_time | real\_time |
| :- | -: | -: | -: | -: |
| BM\_SnapshotSharedPtr/1/threads:1 | 22599685 | 1 | 49 ns | 49 ns |
| BM\_SnapshotSharedPtr/1/threads:2 | 20000000 | 2 | 26 ns | 13 ns |
| BM\_SnapshotSharedPtr/1/threads:4 | 61167436 | 4 | 17 ns | 6 ns |
| BM\_SnapshotSharedPtr/1/threads:8 | 64690592 | 8 | 10 ns | 2 ns |
| BM\_SnapshotSharedPtr/1/threads:16 | 94622832 | 16 | 8 ns | 2 ns |
| BM\_SnapshotSharedPtr/1/threads:32 | 101194048 | 32 | 7 ns | 1 ns |
| BM\_SnapshotSharedPtr/1/threads:64 | 74136576 | 64 | 7 ns | 1 ns |
| BM\_SnapshotSharedPtr/4/threads:1 | 21964534 | 1 | 37 ns | 37 ns |
| BM\_SnapshotSharedPtr/4/threads:2 | 37487102 | 2 | 16 ns | 8 ns |
| BM\_SnapshotSharedPtr/4/threads:4 | 48596816 | 4 | 15 ns | 5 ns |
| BM\_SnapshotSharedPtr/4/threads:8 | 77163960 | 8 | 9 ns | 2 ns |
| BM\_SnapshotSharedPtr/4/threads:16 | 100128368 | 16 | 7 ns | 2 ns |
| BM\_SnapshotSharedPtr/4/threads:32 | 93047680 | 32 | 7 ns | 1 ns |
| BM\_SnapshotSharedPtr/4/threads:64 | 80436992 | 64 | 8 ns | 2 ns |

#### BM\_SnapshotSharedPtrThreadLocal

| name | iterations | threads | cpu\_time | real\_time |
| :- | -: | -: | -: | -: |
| BM\_SnapshotSharedPtrThreadLocal/1/threads:1 | 5359667 | 1 | 121 ns | 121 ns |
| BM\_SnapshotSharedPtrThreadLocal/1/threads:2 | 5128610 | 2 | 117 ns | 58 ns |
| BM\_SnapshotSharedPtrThreadLocal/1/threads:4 | 4693200 | 4 | 154 ns | 47 ns |
| BM\_SnapshotSharedPtrThreadLocal/1/threads:8 | 5268048 | 8 | 127 ns | 36 ns |
| BM\_SnapshotSharedPtrThreadLocal/1/threads:16 | 4184160 | 16 | 153 ns | 36 ns |
| BM\_SnapshotSharedPtrThreadLocal/1/threads:32 | 5058400 | 32 | 158 ns | 35 ns |
| BM\_SnapshotSharedPtrThreadLocal/1/threads:64 | 4077120 | 64 | 140 ns | 24 ns |
| BM\_SnapshotSharedPtrThreadLocal/4/threads:1 | 6381685 | 1 | 104 ns | 104 ns |
| BM\_SnapshotSharedPtrThreadLocal/4/threads:2 | 5958264 | 2 | 118 ns | 61 ns |
| BM\_SnapshotSharedPtrThreadLocal/4/threads:4 | 4127656 | 4 | 140 ns | 43 ns |
| BM\_SnapshotSharedPtrThreadLocal/4/threads:8 | 4579024 | 8 | 143 ns | 38 ns |
| BM\_SnapshotSharedPtrThreadLocal/4/threads:16 | 5272016 | 16 | 147 ns | 33 ns |
| BM\_SnapshotSharedPtrThreadLocal/4/threads:32 | 4403968 | 32 | 160 ns | 29 ns |
| BM\_SnapshotSharedPtrThreadLocal/4/threads:64 | 5054528 | 64 | 114 ns | 25 ns |

#### BM\_Updates

| name | iterations | threads | cpu\_time | real\_time |
| :- | -: | -: | -: | -: |
| BM\_Updates/1/threads:1 | 1894659 | 1 | 368 ns | 368 ns |
| BM\_Updates/1/threads:2 | 816074 | 2 | 751 ns | 490 ns |
| BM\_Updates/1/threads:4 | 1149612 | 4 | 713 ns | 475 ns |
| BM\_Updates/1/threads:8 | 989832 | 8 | 592 ns | 428 ns |
| BM\_Updates/1/threads:16 | 1524816 | 16 | 399 ns | 302 ns |
| BM\_Updates/1/threads:32 | 1001440 | 32 | 703 ns | 436 ns |
| BM\_Updates/1/threads:64 | 1577280 | 64 | 662 ns | 406 ns |
| BM\_Updates/4/threads:1 | 659312 | 1 | 1091 ns | 1311 ns |
| BM\_Updates/4/threads:2 | 672904 | 2 | 1770 ns | 2264 ns |
| BM\_Updates/4/threads:4 | 547044 | 4 | 1415 ns | 1457 ns |
| BM\_Updates/4/threads:8 | 488376 | 8 | 1411 ns | 1682 ns |
| BM\_Updates/4/threads:16 | 580800 | 16 | 1084 ns | 1220 ns |
| BM\_Updates/4/threads:32 | 668096 | 32 | 1373 ns | 1309 ns |
| BM\_Updates/4/threads:64 | 690112 | 64 | 1035 ns | 1202 ns |

### Metric collection

| header | value |
| --- | --- |
| executable | ./build/rel-clang/simple_rcu/lock_free_metric_benchmark |
| num_cpus | 4 |
| mhz_per_cpu | 3437 |
| library_build_type | release |

#### BM\_LocalTwoThreads

| name | iterations | threads | cpu\_time | real\_time |
| :- | -: | -: | -: | -: |
| BM\_LocalTwoThreads/threads:2 | 8400086 | 2 | 82 ns | 41 ns |

#### BM\_MultiThreadedUpdateView

| name | iterations | threads | cpu\_time | real\_time |
| :- | -: | -: | -: | -: |
| BM\_MultiThreadedUpdateView/threads:1 | 65144996 | 1 | 10 ns | 10 ns |
| BM\_MultiThreadedUpdateView/threads:1\_BigO |  | 1 |  |  |
| BM\_MultiThreadedUpdateView/threads:1\_RMS |  | 1 |  |  |
| BM\_MultiThreadedUpdateView/threads:2 | 13206618 | 2 | 54 ns | 27 ns |
| BM\_MultiThreadedUpdateView/threads:4 | 7431392 | 4 | 96 ns | 24 ns |
| BM\_MultiThreadedUpdateView/threads:8 | 9100808 | 8 | 87 ns | 19 ns |
| BM\_MultiThreadedUpdateView/threads:16 | 11609552 | 16 | 60 ns | 10 ns |
| BM\_MultiThreadedUpdateView/threads:32 | 15849248 | 32 | 37 ns | 4 ns |
| BM\_MultiThreadedUpdateView/threads:64 | 37265088 | 64 | 18 ns | 2 ns |

#### BM\_MultiThreadedUpdateThreadLocal

| name | iterations | threads | cpu\_time | real\_time |
| :- | -: | -: | -: | -: |
| BM\_MultiThreadedUpdateThreadLocal/threads:1 | 38321387 | 1 | 18 ns | 18 ns |
| BM\_MultiThreadedUpdateThreadLocal/threads:1\_BigO |  | 1 |  |  |
| BM\_MultiThreadedUpdateThreadLocal/threads:1\_RMS |  | 1 |  |  |
| BM\_MultiThreadedUpdateThreadLocal/threads:2 | 12094004 | 2 | 50 ns | 25 ns |
| BM\_MultiThreadedUpdateThreadLocal/threads:4 | 6863060 | 4 | 101 ns | 26 ns |
| BM\_MultiThreadedUpdateThreadLocal/threads:8 | 12369072 | 8 | 55 ns | 13 ns |
| BM\_MultiThreadedUpdateThreadLocal/threads:16 | 17277680 | 16 | 40 ns | 8 ns |
| BM\_MultiThreadedUpdateThreadLocal/threads:32 | 20498688 | 32 | 31 ns | 5 ns |
| BM\_MultiThreadedUpdateThreadLocal/threads:64 | 27623744 | 64 | 24 ns | 3 ns |

#### BM\_MultiThreadedCollect

| name | iterations | threads | cpu\_time | real\_time |
| :- | -: | -: | -: | -: |
| BM\_MultiThreadedCollect/threads:1 | 20037240 | 1 | 35 ns | 35 ns |
| BM\_MultiThreadedCollect/threads:1\_BigO |  | 1 |  |  |
| BM\_MultiThreadedCollect/threads:1\_RMS |  | 1 |  |  |
| BM\_MultiThreadedCollect/threads:2 | 5775376 | 2 | 104 ns | 52 ns |
| BM\_MultiThreadedCollect/threads:4 | 6136400 | 4 | 118 ns | 30 ns |
| BM\_MultiThreadedCollect/threads:8 | 8989576 | 8 | 126 ns | 27 ns |
| BM\_MultiThreadedCollect/threads:16 | 8255344 | 16 | 77 ns | 11 ns |
| BM\_MultiThreadedCollect/threads:32 | 9509376 | 32 | 68 ns | 7 ns |
| BM\_MultiThreadedCollect/threads:64 | 11411328 | 64 | 59 ns | 4 ns |

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
