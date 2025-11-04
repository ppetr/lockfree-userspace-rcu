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
#ifdef __cpp_lib_atomic_lock_free_type_aliases
  using Index = typename std::atomic_signed_lock_free::value_type;
#else
  using Index = int_fast8_t;
#endif
#ifdef __cpp_lib_atomic_is_always_lock_free
  static_assert(std::atomic<Index>::is_always_lock_free,
                "Not lock-free on this architecture, please report this as a "
                "bug on the project's GitHub page");
#endif

  constexpr static Index kIndexMask = 3;
  constexpr static Index kByRightMask = 4;

  Index left_;
  std::atomic<Index> passing_;
  Index right_;
  std::array<T, 3> values_;
};

template <typename T>
class LocalLockFreeMetric {
 public:
  void Update(T value) {
    exchange_.Left().seq.emplace_back(value);
    const int_fast32_t last_start = exchange_.Left().start;
    const auto [next, exchanged] = exchange_.PassLeft();
    if (exchanged) {
      next->Reset(update_index_);
    } else if (last_start > next->start) {
      next->EraseFirstN(last_start - next->start);
    }
    CHECK_EQ(next->start + next->seq.size(), update_index_)
        << "next.start = " << next->start;
    update_index_++;
    next->seq.push_back(std::move(value));
  }

  std::deque<T> Collect() {
    Slice* const next = exchange_.PassRight().first;
    const int_fast32_t seen = collect_index_ - next->start;
    if (seen < 0) {
      LOG(FATAL) << "Missing range " << collect_index_ << ".." << next->start;
    } else if (seen < next->seq.size()) {
      CHECK_GE(seen, 0) << "next.start = " << next->start
                        << ", next.seq.size() = " << next->seq.size()
                        << ", collect_index_ = " << collect_index_;
      next->EraseFirstN(seen);
      collect_index_ += next->seq.size();
      return std::exchange(next->seq, {});
    } else {
      next->Reset(collect_index_);
      return {};
    }
  }

 private:
  struct Slice {
    void EraseFirstN(int_fast32_t n) {
      seq.erase(seq.begin(), seq.begin() + n);
      start += n;
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
