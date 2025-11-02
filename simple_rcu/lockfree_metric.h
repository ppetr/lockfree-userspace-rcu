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

#include <atomic>
#include <cstdint>
#include <deque>
#include <optional>
#include <type_traits>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "simple_rcu/local_3state_rcu.h"

namespace simple_rcu {

template <typename T>
class Local3StateExchange {
 public:
  // TODO: Symmetry - everything except `previous_...` indices.
  Local3StateExchange()
      : left_index_(0),
        previous_left_(1),
        passing_index_(1),
        right_index_(2),
        previous_right_(0) {}

  const T& Left() const { return values_[left_index_]; }
  T& Left() { return values_[left_index_]; }

  std::pair<T * absl_nonnull, bool> PassLeft() {
    const Index old_index = left_index_;
    left_index_ =
        passing_index_.exchange(left_index_, std::memory_order_acq_rel);
    return {&Left(), std::exchange(previous_left_, old_index) != old_index};
  }

  const T& Right() const { return values_[right_index_]; }
  T& Right() { return values_[right_index_]; }

  std::pair<T * absl_nonnull, bool> PassRight() {
    const Index old_index = right_index_;
    right_index_ =
        passing_index_.exchange(right_index_, std::memory_order_acq_rel);
    return {&Right(), std::exchange(previous_right_, old_index) != old_index};
  }

 private:
  // TODO: proper type like in L3SR
  using Index = int_fast8_t;

  Index left_index_;
  Index previous_left_;
  std::atomic<Index> passing_index_;
  Index right_index_;
  Index previous_right_;
  std::array<T, 3> values_;
};

template <typename T>
class LocalLockFreeMetric {
 public:
  void Update(T value) {
    exchange_.Left().seq.emplace_back(value);
    auto [next, exchanged] = exchange_.PassLeft();
    if (next->seq.empty()) {
      next->start = update_index_;
    }
    CHECK_EQ(next->start + next->seq.size(), update_index_)
        << "next.start = " << next->start;
    update_index_++;
    next->seq.push_back(std::move(value));
  }

  std::deque<T> Collect() {
    auto [next, exchanged] = exchange_.PassRight();
    const int_fast32_t seen = collect_index_ - next->start;
    next->start = collect_index_;
    if (seen < 0) {
      collect_index_ += next->seq.size();
      return std::exchange(next->seq, {});
    } else if (seen < next->seq.size()) {
      CHECK_GE(seen, 0) << "next.start = " << next->start
                        << ", next.seq.size() = " << next->seq.size()
                        << ", collect_index_ = " << collect_index_;
      next->seq.erase(next->seq.begin(), next->seq.begin() + seen);
      collect_index_ += next->seq.size();
      return std::exchange(next->seq, {});
    } else {
      next->seq.clear();
      return {};
    }
  }

 private:
  struct Slice {
    int_fast32_t start = 0;
    std::deque<T> seq;
  };

  int_fast32_t update_index_ = 0;
  int_fast32_t collect_index_ = 0;
  Local3StateExchange<Slice> exchange_;
};

}  // namespace simple_rcu
