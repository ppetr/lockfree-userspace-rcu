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

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "simple_rcu/local_3state_exchange.h"
#include "simple_rcu/thread_local.h"

namespace simple_rcu {

template <typename C, typename D = C>
struct OperatorPlus {
  static constexpr inline D NoOp() { return D{}; }

  static inline C& Update(C& target, D diff) {
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
  // whether this update received a new version from the other thread.  If it's
  // desired to obtain `C` after `diff` is applied, call another `Update` with
  // a no-op operation, as in:
  //
  //    ttc.Update<false>(42);
  //    const auto& value_after_update = ttc.Update<false>(0).first;
  //
  // The returned reference is valid only until another call to `Update`.
  template <bool Right>
  inline std::pair<const C&, bool> Update(D diff) {
    exchange_.template side<Right>().ref().Append(diff);

    std::optional<Slice> prev_copy(std::nullopt);
    const auto next = exchange_.template side<Right>().Pass(
        [&prev_copy](const Slice& ref) { prev_copy = ref; });
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

  // Just as `Update` above, but issues a `U::NoOp()` operation after `diff` to
  // ensure `diff` is visible to the caller in the result.
  //
  // WARNING: When calling this method (by any thread) it's no longer
  // meaningful to observe exchanges, because they can be just no-ops.
  template <bool Right>
  inline const C& UpdateSelfVisible(D diff) {
    Update<Right>(std::move(diff));
    return Update<Right>(U::NoOp()).first;
  }

 private:
  class Slice final {
   public:
    explicit Slice(C initial)
        : collected_(std::move(initial)), last_{U::NoOp()} {}

    inline void Append(D diff) {
      U::Update(collected_, std::exchange(last_, std::move(diff)));
    }

    C collected_;
    // Populated on first `Update` and holds a value ever since.
    D last_;
  };

  Local3StateExchange<Slice> exchange_;
};

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_TWO_THREAD_CONCURRENT_H
