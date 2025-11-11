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
#include "absl/functional/function_ref.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "simple_rcu/local_3state_rcu.h"
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
  };

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

template <typename T>
class LocalLockFreeMetric {
 public:
  void Update(T value) {
    exchange_.Left().seq.emplace_back(value);
    const int_fast32_t last_start = exchange_.Left().start;
    const auto [next, exchanged] = exchange_.PassLeft();
    if (exchanged) {
      // The previous value was at `update_index_ - 1`, which has now been seen
      // by the collecting side.
      next->Reset(update_index_);
    } else if (auto advance = last_start - next->start; advance > 0) {
      ABSL_CHECK_EQ(advance, next->seq.size() - 1);
      next->KeepJustLast();
    }
    ABSL_CHECK_EQ(next->start + next->seq.size(), update_index_)
        << "next.start = " << next->start;
    update_index_++;
    next->seq.push_back(std::move(value));
  }

  std::deque<T> Collect() {
    Slice* const next = exchange_.PassRight().first;
    const int_fast32_t seen = collect_index_ - next->start;
    if (seen < 0) {
      ABSL_LOG(FATAL) << "Missing range " << collect_index_ << ".."
                      << next->start;
    } else if (seen < next->seq.size()) {
      if (seen > 0) {
        ABSL_CHECK_EQ(seen, next->seq.size() - 1)
            << "next.start = " << next->start
            << ", next.seq.size() = " << next->seq.size()
            << ", collect_index_ = " << collect_index_;
        next->KeepJustLast();
      }
      ABSL_LOG(INFO) << "seen = " << seen << ", remaining " << next->seq.size();
      collect_index_ += next->seq.size();
      return std::exchange(next->seq, {});
    } else {
      next->Reset(collect_index_);
      return {};
    }
  }

 private:
  struct Slice {
    void KeepJustLast() {
      start += seq.size() - 1;
      seq.erase(seq.begin(), seq.end() - 1);
    }

    void Reset(int_fast32_t new_start) {
      seq.clear();
      start = new_start;
    }

    int_fast32_t start = 0;
    std::deque<T> seq;
  };

  int_fast32_t update_index_ = 0;
  int_fast32_t collect_index_ = 0;
  Local3StateExchange<Slice> exchange_;
};

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_LOCKFREE_METRIC_H
