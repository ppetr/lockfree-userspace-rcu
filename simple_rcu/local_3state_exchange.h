// Copyright 2022 Google LLC
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

#ifndef _SIMPLE_RCU_3STATE_EXCHANGE_H
#define _SIMPLE_RCU_3STATE_EXCHANGE_H

#include <array>
#include <atomic>

#include "simple_rcu/lock_free_int.h"

namespace simple_rcu {

template <typename T>
class Local3StateExchange {
 public:
  Local3StateExchange() : left_(0), passing_(1), right_(2) {}

  inline const T& Left() const { return values_[left_]; }
  inline T& Left() { return values_[left_]; }

  inline std::pair<T&, bool> PassLeft() {
    const Index received = passing_.exchange(left_, std::memory_order_acq_rel);
    left_ = received & kIndexMask;
    return {Left(), received & kByRightMask};
  }

  inline const T& Right() const { return values_[right_]; }
  inline T& Right() { return values_[right_]; }

  inline std::pair<T&, bool> PassRight() {
    const Index received =
        passing_.exchange(right_ | kByRightMask, std::memory_order_acq_rel);
    right_ = received & kIndexMask;
    return {Right(), !(received & kByRightMask)};
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

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_3STATE_EXCHANGE_H
