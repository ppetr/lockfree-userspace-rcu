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
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "absl/synchronization/mutex.h"
#include "simple_rcu/local_3state_exchange.h"
#include "simple_rcu/thread_local.h"

namespace simple_rcu {

// Collects values of type `D` from one thread into values of type `C` available
// in another thread using just lock-free operations. Each call to `Update` or
// `Collect` uses just a single atomic, lock-free operation.
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
template <typename C, typename D = C>
class LocalLockFreeMetric {
 public:
  static_assert(std::is_default_constructible_v<C>,
                "`C` must be a default-constructible type");
  static_assert(std::is_move_constructible_v<C> && std::is_move_assignable_v<C>,
                "`C` must be a movable type");
  static_assert(std::is_copy_constructible_v<D> && std::is_copy_assignable_v<D>,
                "`D` must be a copyable type");

  void Update(D value) {
    int_fast32_t last_start;
    {
      Slice& prev = exchange_.Left();
      prev.Append(value);
      last_start = prev.start();
    }
    const auto [next, exchanged] = exchange_.PassLeft();
    if (exchanged) {
      // The previous value was at `update_index_ - 1`, which has now been seen
      // by the collecting side.
      ABSL_CHECK(next.empty());
      next.Reset(update_index_);
    } else if (auto advance = last_start - next.start(); advance > 0) {
      ABSL_CHECK_EQ(advance, next.size() - 1);
      next.KeepJustLast();
    }
    ABSL_CHECK_EQ(next.end(), update_index_) << "next.end = " << next.end();
    update_index_++;
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
        ABSL_CHECK_EQ(seen, next.size() - 1)
            << "next.start = " << next.start()
            << ", next.size() = " << next.size()
            << ", collect_index_ = " << collect_index_;
        next.KeepJustLast();
      }
      // ABSL_VLOG(4) << "seen = " << seen << ", remaining " << next.size();
      collect_index_ += next.size();
      return next.CollectAndReset();
    } else {
      ABSL_CHECK(next.empty());
      next.Reset(collect_index_);
      return C{};
    }
  }

 private:
  class Slice {
   public:
    int_fast32_t start() const { return start_; }
    int_fast32_t end() const { return end_; }
    int_fast32_t size() const { return end_ - start_; }
    bool empty() const { return !last_.has_value(); }

    void Append(D value) {
      if (last_.has_value()) {
        collected_ += *std::move(last_);
      }
      last_ = std::move(value);
      end_++;
    }

    void KeepJustLast() {
      start_ = end_ - 1;
      collected_ = C{};
    }

    void Reset(int_fast32_t new_start) {
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

  int_fast32_t update_index_ = 0;
  int_fast32_t collect_index_ = 0;
  Local3StateExchange<Slice> exchange_;
};

template <typename C, typename D = C>
class LockFreeMetric
    : public std::enable_shared_from_this<LockFreeMetric<C, D>> {
 protected:
  struct ConstructOnlyWithMakeShared {};

 public:
  LockFreeMetric(ConstructOnlyWithMakeShared) {}

  static std::shared_ptr<LockFreeMetric> New() {
    return std::make_shared<LockFreeMetric>(ConstructOnlyWithMakeShared{});
  }

  static void Update(D value, std::shared_ptr<LockFreeMetric> ptr) {
    ThreadLocal<LocalMetric, LockFreeMetric>::try_emplace(ptr, ptr)
        .first.local()
        .local()
        .Update(std::move(value));
  }

  // Calls `Update` using `shared_from_this()`.
  void Update(D value) { Update(std::move(value), this->shared_from_this()); }

  static std::vector<C> Collect(std::shared_ptr<LockFreeMetric> ptr) {
    std::vector<C> result;
    if (ptr == nullptr) {
      return result;
    }
    absl::MutexLock mutex(&ptr->lock_);
    result.reserve(ptr->locals_.size());
    for (Local* l : ptr->locals_) {
      result.push_back(l->Collect());
    }
    return result;
  }

  // Calls `Collect` using `shared_from_this()`.
  std::vector<C> Collect() { return Collect(this->shared_from_this()); }

 private:
  using Local = LocalLockFreeMetric<C, D>;

  class LocalMetric {
   public:
    LocalMetric(std::shared_ptr<LockFreeMetric> metric)
        : metric_(metric), local_(std::make_unique<Local>()) {
      absl::MutexLock mutex(&metric->lock_);
      metric->locals_.insert(local_.get());
    }
    LocalMetric(LocalMetric&&) = default;
    LocalMetric& operator=(LocalMetric&&) = default;

    ~LocalMetric() {
      std::shared_ptr<LockFreeMetric> metric = metric_.lock();
      if (metric != nullptr) {
        absl::MutexLock mutex(&metric->lock_);
        metric->locals_.erase(local_.get());
      }
    }

    Local& local() { return *local_; }

   private:
    // This weak_ptr duplicates the one in `ThreadLocal`. Figure out if they
    // can be shared somehow.
    std::weak_ptr<LockFreeMetric> metric_;
    std::unique_ptr<Local> local_;
  };

  absl::Mutex lock_;
  absl::flat_hash_set<Local*> locals_ ABSL_GUARDED_BY(lock_);
  friend struct LocalMetric;
};

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_LOCK_FREE_METRIC_H
