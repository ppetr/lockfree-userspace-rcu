// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _SIMPLE_RCU_LOCK_FREE_METRIC_H
#define _SIMPLE_RCU_LOCK_FREE_METRIC_H

#include <cstdint>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "simple_rcu/thread_local.h"
#include "simple_rcu/two_thread_concurrent.h"

namespace simple_rcu {

template <typename C, typename D = C>
class LocalLockFreeMetric;

// Implements a local (just between 2 threads) lock- (wait-)free metric
// collection, the "update" part.
//
// This class allows communication just between two threads and is a building
// block for `LockFreeMetric`. In vast majority of cases you'll want to use
// `LockFreeMetric`, which works for arbitrary number of threads.
template <typename C, typename D = C>
class LocalLockFreeMetricUpdate {
 public:
  static_assert(std::is_default_constructible_v<C>,
                "`C` must be a default-constructible type");
  static_assert(std::is_move_constructible_v<C> && std::is_move_assignable_v<C>,
                "`C` must be a movable type");
  static_assert(std::is_copy_constructible_v<D> && std::is_copy_assignable_v<D>,
                "`D` must be a copyable type");

  inline void Update(D value) {
    exchange_.template Update<false>(std::move(value));
  }

 private:
  struct Metric {
    template <
        typename E,
        typename std::enable_if_t<
            std::is_same_v<absl::remove_cvref_t<E>, std::optional<D>>, int> = 0>
    inline Metric& operator+=(E&& increment_) {
      if (increment_.has_value()) {
        value += *std::forward<E>(increment_);
      } else {
        value = C{};
      }
      return *this;
    }

    C value;
  };

  TwoThreadConcurrent<Metric, std::optional<D>> exchange_;

  friend class LocalLockFreeMetric<C, D>;
};

// Collects atomically values of type `D` from one thread into values of type
// `C` available in another thread. Each call to `Update` or `Collect` uses
// just a single atomic, wait-free operation.
//
// See class `LockFreeMetric` below for details about requirements on `C` and
// `D`.
//
// This class allows communication just between two threads and is a building
// block for `LockFreeMetric`. In vast majority of cases you'll want to use
// `LockFreeMetric`, which works for arbitrary number of threads.
template <typename C, typename D>
class LocalLockFreeMetric final : public LocalLockFreeMetricUpdate<C, D> {
 public:
  // Returns a collection of all `D` values passed to `Update` (by the other
  // thread) accumulated into `C{}` using `operator+=(C&, D)` since the last
  // call to `Collect`.
  ABSL_MUST_USE_RESULT C Collect() {
    return LocalLockFreeMetricUpdate<C, D>::exchange_
        .template Update<true>(std::nullopt)
        .first.value;
  }
};

// Collects values of type `D` from one thread into values of type `C`
// available in another thread. Each call to `Update` uses just a single
// atomic, wait-free operation. (`Collect` is heavier and uses standard
// `absl::Mutex` internally.)
//
// `C` must implement a thread-compatible `operator+=(D)` (or a compatible one)
// that will be called to collect values from `Update(D)` until `Collect()` is
// called.  See the `static_assert`s below for further requirements on these
// types.
//
// The trade-off to allow this lock-free implementation is that for each
// `Update(d)` parameter `d`, the `operator+=(d)` is called twice on two
// separate copies of `C`. And some of these calls can be delayed to the next
// `Collect()` call (this is why it should be thread-compatible).
//
// Examples:
//
// Notably the above requirements are satisfied by all numerical types. Which
// allows them to be accumulated lock-free regardless of their
// `std::atomic<C>::is_always_lock_free`.
//
// It's also easy to create wrappers around standard collections and implement
// `+=` to add values into them (see the for example `BackCollection` in the
// ..._test.cc file). This effectively implements a lock-free channel.
//
// A plain gauge that tracks just the lastest value can be implemented by
//
//     C& operator+=(D value) { *this = std::move(value); }
//
// Finally, any class and operations on can be wrapped as a metric by setting
// `D = std::function<void(C&)>`, see `AnyFunctor` example in
// `lock_free_metric_test.cc`.
template <typename C, typename D = C>
class LockFreeMetric {
 public:
  using View = LocalLockFreeMetricUpdate<C, D>;

  LockFreeMetric() = default;

  // Updates this thread's instance of `C` with `value` using
  // `operator+=(C&,D)`. As soon as this method returns, the effect of `value`
  // will be visible in the result of the next call to `Collect`.
  //
  // The first call a thread calls this method might be slower in order to
  // construct a thread-local channel for it. All subsequent alls are very
  // fast.
  //
  // Thread-safe, wait-free.
  inline void Update(D value) {
    locals_.try_emplace().first.Update(std::move(value));
  }

  // Returns a thread-local reference that allows updating the metric directly.
  // Using `View` directly skips accessing the internal `LockFreeMetric`'s
  // `thread_local` variable, making updates truly wait-free regardless of the
  // `thread_local` implementation.
  //
  // The returned reference is valid only for the current thread.
  //
  // Thread-safe, wait-free.
  inline View& ThreadLocalView() noexcept {
    return locals_.try_emplace().first;
  }

  // Collects all `C` instances from all threads. Each element of the returned
  // vector contains the accumulated value for a single thread. The elements
  // are in no particular order. By calling this method, all threads' `C`
  // instances are reset to `C{}`.
  //
  // Thread-safe.
  std::vector<C> Collect() {
    std::lock_guard mutex(collect_lock_);
    auto pruned = locals_.PruneAndList();
    std::vector<C> result;
    result.reserve(pruned.current.size() + pruned.abandoned.size());
    for (Local* i : pruned.current) {
      result.push_back(i->Collect());
    }
    for (auto& i : pruned.abandoned) {
      result.push_back(i->value.Collect());
    }
    return result;
  }

 private:
  using Local = LocalLockFreeMetric<C, D>;

  absl::Mutex collect_lock_;
  ThreadLocalDelayed<Local> locals_;
};

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_LOCK_FREE_METRIC_H
