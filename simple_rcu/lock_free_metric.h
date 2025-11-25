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
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "simple_rcu/local_3state_exchange.h"
#include "simple_rcu/thread_local.h"

namespace simple_rcu {

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
template <typename C, typename D = C>
class LocalLockFreeMetric {
 public:
  static_assert(std::is_default_constructible_v<C>,
                "`C` must be a default-constructible type");
  static_assert(std::is_move_constructible_v<C> && std::is_move_assignable_v<C>,
                "`C` must be a movable type");
  static_assert(std::is_copy_constructible_v<D> && std::is_copy_assignable_v<D>,
                "`D` must be a copyable type");

  inline void Update(D value) {
    int_fast32_t last_start, last_end;
    {
      Slice& prev = exchange_.Left();
      prev.Append(value);
      last_start = prev.start();
      last_end = prev.end();
    }
    const auto [next, exchanged] = exchange_.PassLeft();
    if (exchanged) {
      ABSL_DCHECK(next.empty());
      // The previous value was at `last_end - 1`, which has now been seen by
      // the collecting side.
      next.Reset(last_end - 1);
    } else if (auto advance = last_start - next.start(); advance > 0) {
      ABSL_DCHECK_EQ(advance, next.size() - 1);
      next.KeepJustLast();
    }
    next.Append(std::move(value));
  }

  // Returns a collection of all `D` values passed to `Update` (by the other
  // thread) accumulated into `C{}` using `operator+=(C&, D)` since the last
  // call to `Collect`.
  ABSL_MUST_USE_RESULT C Collect() {
    Slice& next = exchange_.PassRight().first;
    // On return `next.empty()` holds.
    const int_fast32_t seen = collect_index_ - next.start();
    if (seen < 0) {
      ABSL_LOG(FATAL) << "Missing range " << collect_index_ << ".."
                      << next.start();
      return C{};  // Unreachable.
    } else if (seen < next.size()) {
      if (seen > 0) {
        ABSL_DCHECK_EQ(seen, next.size() - 1)
            << "next.start = " << next.start()
            << ", next.size() = " << next.size()
            << ", collect_index_ = " << collect_index_;
        next.KeepJustLast();
      }
      // ABSL_VLOG(4) << "seen = " << seen << ", remaining " << next.size();
      collect_index_ += next.size();
      return next.CollectAndReset();
    } else {
      ABSL_DCHECK(next.empty());
      next.Reset(collect_index_);
      return C{};
    }
  }

 private:
  class Slice final {
   public:
    inline int_fast32_t start() const { return start_; }
    inline int_fast32_t end() const { return end_; }
    inline int_fast32_t size() const { return end_ - start_; }
    inline bool empty() const { return !last_.has_value(); }

    inline void Append(D value) {
      if (last_.has_value()) {
        collected_ += std::exchange(*last_, std::move(value));
      } else {
        last_ = std::move(value);
      }
      end_++;
    }

    inline void KeepJustLast() {
      start_ = end_ - 1;
      collected_ = C{};
    }

    inline void Reset(int_fast32_t new_start) {
      if (!empty()) {
        collected_ = C{};
        last_.reset();
      }
      end_ = (start_ = new_start);
    }

    C CollectAndReset() {
      start_ = end_;
      if (last_.has_value()) {
        collected_ += *std::move(last_);
        last_.reset();
      }
      return std::exchange(collected_, C{});
    }

   private:
    int_fast32_t start_ = 0;
    int_fast32_t end_ = 0;
    C collected_;
    // Holds a value iff `end_ > start_`.
    std::optional<D> last_;
  };

  int_fast32_t collect_index_ = 0;
  Local3StateExchange<Slice> exchange_;
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
  LockFreeMetric() = default;

  // Updates this thread's instance of `C` with `value` using
  // `operator+=(C&,D)`. As soon as this method returns, the effect of `value`
  // will be visible in the result of the next call to `Collect`.
  //
  // The first call a thread calls this method might be slower in order to
  // construct a thread-local channel for it. All subsequent alls are very
  // fast.
  //
  // Thread-safe, lock-free.
  inline void Update(D value) {
    locals_.try_emplace().first.Update(std::move(value));
  }

  // Collects all `C` instances from all threads. Each element of the returned
  // vector contains the accumulated value for a single thread. The elements
  // are in no particular order. By calling this method, all threads' `C`
  // instances are reset to `C{}`.
  //
  // Thread-safe.
  std::vector<C> Collect() {
    absl::MutexLock mutex(&collect_lock_);
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
