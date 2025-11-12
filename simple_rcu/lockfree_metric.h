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

#ifndef _SIMPLE_RCU_LOCKFREE_METRIC_H
#define _SIMPLE_RCU_LOCKFREE_METRIC_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <optional>
#include <type_traits>
#include <utility>

// TODO #include "absl/base/nullability.h"
#include "absl/base/attributes.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "simple_rcu/lock_free_int.h"

// TODO
#ifndef absl_nonnull
#define absl_nonnull
#endif

namespace simple_rcu {

template <typename T>
class Local3StateExchange {
 public:
  Local3StateExchange() : left_(0), passing_(1), right_(2) {}

  const T& Left() const { return values_[left_]; }
  T& Left() { return values_[left_]; }

  std::pair<T * absl_nonnull, bool> PassLeft() {
    const Index received = passing_.exchange(left_, std::memory_order_acq_rel);
    left_ = received & kIndexMask;
    return {&Left(), received & kByRightMask};
  }

  const T& Right() const { return values_[right_]; }
  T& Right() { return values_[right_]; }

  std::pair<T * absl_nonnull, bool> PassRight() {
    const Index received =
        passing_.exchange(right_ | kByRightMask, std::memory_order_acq_rel);
    right_ = received & kIndexMask;
    return {&Right(), !(received & kByRightMask)};
  }

 private:
  using Index = AtomicSignedLockFree;
  static_assert(!std::is_void_v<Index> &&
                    std::atomic<Index>::is_always_lock_free,
                "Not lock-free on this architecture, please report this as a "
                "bug on the project's GitHub page");

  constexpr static Index kIndexMask = 3;
  constexpr static Index kByRightMask = 4;

  Index left_;
  std::atomic<Index> passing_;
  Index right_;
  std::array<T, 3> values_{T{}, T{}, T{}};
};

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
// separate copies of `C`. And some of these calls can occur only during the
// next `Collect()` call.
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
    exchange_.Left().Append(value);
    const int_fast32_t last_start = exchange_.Left().start();
    const auto [next, exchanged] = exchange_.PassLeft();
    if (exchanged) {
      // The previous value was at `update_index_ - 1`, which has now been seen
      // by the collecting side.
      next->Reset(update_index_);
    } else if (auto advance = last_start - next->start(); advance > 0) {
      ABSL_CHECK_EQ(advance, next->size() - 1);
      next->KeepJustLast();
    }
    ABSL_CHECK_EQ(next->end(), update_index_) << "next.end = " << next->end();
    update_index_++;
    next->Append(std::move(value));
  }

  // Returns a collection of all `D` values passed to `Update` (by the other
  // thread) accumulated into `C{}` using `operator+=(C&, D)` since the last
  // call to `Collect`.
  ABSL_MUST_USE_RESULT C Collect() {
    Slice* const next = exchange_.PassRight().first;
    const int_fast32_t seen = collect_index_ - next->start();
    if (seen < 0) {
      ABSL_LOG(FATAL) << "Missing range " << collect_index_ << ".."
                      << next->start();
      return C{};  // Unreachable.
    } else if (seen < next->size()) {
      if (seen > 0) {
        ABSL_CHECK_EQ(seen, next->size() - 1)
            << "next.start = " << next->start()
            << ", next.size() = " << next->size()
            << ", collect_index_ = " << collect_index_;
        next->KeepJustLast();
      }
      // ABSL_VLOG(4) << "seen = " << seen << ", remaining " << next->size();
      collect_index_ += next->size();
      return next->CollectAndReset();
    } else {
      next->Reset(collect_index_);
      return C{};
    }
  }

 private:
  class Slice {
   public:
    int_fast32_t start() const { return start_; }
    int_fast32_t end() const { return end_; }
    int_fast32_t size() const { return end_ - start_; }

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
      collected_ = C{};
      last_.reset();
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

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_LOCKFREE_METRIC_H
