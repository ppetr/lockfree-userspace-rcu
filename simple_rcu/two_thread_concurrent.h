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

#ifndef _SIMPLE_RCU_TWO_THREAD_CONCURRENT_H
#define _SIMPLE_RCU_TWO_THREAD_CONCURRENT_H

#include <optional>
#include <type_traits>
#include <utility>

#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "simple_rcu/local_3state_exchange.h"

namespace simple_rcu {

template <typename C, typename D = C>
struct OperatorPlus {
  static constexpr inline D NoOp() { return D{}; }

  static inline C& Apply(C& target, D diff) {
    return target += std::move(diff);
  }
};

template <typename C, typename D = C, typename U = OperatorPlus<C, D>>
class TwoThreadConcurrent {
 public:
  static_assert(std::is_copy_constructible_v<C> && std::is_copy_assignable_v<C>,
                "`C` must be a copyable type");
  static_assert(std::is_copy_constructible_v<D> && std::is_copy_assignable_v<D>,
                "`D` must be a copyable type");

  template <typename C1 = C, typename std::enable_if_t<
                                 std::is_default_constructible_v<C1>, int> = 0>
  TwoThreadConcurrent() : exchange_(std::in_place, C{}) {}

  explicit TwoThreadConcurrent(const C& initial)
      : exchange_(std::in_place, initial) {}

  // Updates the value using operation `diff`. The type parameter `Right`
  // determines which thread (left/right) is performing the operation.
  //
  // Returns a reference to the value just **before `diff` is applied**, and
  // whether this update received a new version from the other thread. This
  // makes it easy to use this method for procedures such as "exchange" or
  // "compare-and-swap". If it's desired to obtain `C` after `diff` is
  // applied, call `ObserveLast()`.
  //
  // The returned reference is only valid until another call to `Update` with
  // the same `Right` parameter.
  template <bool Right>
  inline std::pair<const C&, bool> Update(D diff) {
    exchange_.template side<Right>().ref().Append(diff);

    std::optional<Slice> prev_copy(std::nullopt);
    const auto next =
        exchange_.template side<Right>().Pass([&prev_copy](auto&& ref) {
          // When exchanging sides, we can optimize by moving the value of
          // `ref`. And `Pass` signals this already by calling the callback
          // with a && type.
          prev_copy = std::forward<decltype(ref)>(ref);
        });
    if (next.past_exchanged) {
      ABSL_CHECK(prev_copy.has_value());
      next.ref.collected_ = std::move(prev_copy)->collected_;
      if (next.exchanged) {
        next.ref.Append(std::move(diff));
      } else {
        next.ref.last_ = std::move(diff);
      }
    } else {
      next.ref.Append(std::move(diff));
    }
    return {next.ref.collected_, next.exchanged};
  }

  // Propagates the last `D` operation to the `C&` returned by the last
  // `Update` method (with the same `Right` parameter), thus ensuring this
  // thread can also observe the effect of the last operation on `C`.
  //
  // Idempotent (until another call to `Update` with the same `Right`
  // parameter).
  template <bool Right>
  const C& ObserveLast() {
    return exchange_.template side<Right>().ref().Append(U::NoOp());
  }

 private:
  class Slice final {
   public:
    explicit Slice(C initial)
        : collected_(std::move(initial)), last_{U::NoOp()} {}

    inline C& Append(D diff) {
      U::Apply(collected_, std::exchange(last_, std::move(diff)));
      return collected_;
    }

    C collected_;
    D last_;
    // When this `Slice` is the middle one passing between the two threads,
    // this holds the most recent operation that can't be yet applied to
    // `collected_`.
  };

  Local3StateExchange<Slice> exchange_;
};

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_TWO_THREAD_CONCURRENT_H
