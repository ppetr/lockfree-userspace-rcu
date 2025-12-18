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
#include "simple_rcu/local_3state_exchange.h"
#include "simple_rcu/thread_local.h"

namespace simple_rcu {

template <typename C, typename D = C>
class TwoThreadConcurrent {
 public:
  static_assert(std::is_default_constructible_v<C>,
                "`C` must be a default-constructible type");
  static_assert(std::is_move_constructible_v<C> && std::is_move_assignable_v<C>,
                "`C` must be a movable type");
  static_assert(std::is_copy_constructible_v<D> && std::is_copy_assignable_v<D>,
                "`D` must be a copyable type");

  template <bool Right>
  inline const C& Update(D value) {
    {
      Slice& prev_ptr = exchange_.template side<Right>().ref();
      prev_ptr.Append(value);
    }

    std::optional<Slice> prev_copy(std::nullopt);
    const auto next = exchange_.template side<Right>().Pass(
        [&prev_copy](const Slice& ref) { prev_copy = ref; });
    ABSL_VLOG(5) << "IN: Right=" << Right << ", exchanged=" << next.exchanged
                 << ", next.past_exchanged=" << next.past_exchanged
                 << ", next.collected_=" << next.ref.collected_
                 << ", prev_copy.collected_=" << prev_copy->collected_
                 << ", value=" << value;
    if (next.past_exchanged) {
      ABSL_CHECK(prev_copy.has_value());
      next.ref.collected_ = std::move(prev_copy)->collected_;
      if (next.exchanged) {
        next.ref.Append(std::move(value));
      } else {
        next.ref.last_ = std::move(value);
      }
    } else {
      next.ref.Append(std::move(value));
    }
    ABSL_VLOG(5) << "OUT: Right=" << Right
                 << ", next.collected_=" << next.ref.collected_;
    return next.ref.collected_;
  }

 private:
  class Slice final {
   public:
    inline void Append(std::optional<D> value) {
      if (ABSL_PREDICT_TRUE(last_.has_value())) {
        collected_ += *std::exchange(last_, std::move(value));
      } else {
        last_ = std::move(value);
      }
    }

    C collected_ = {};
    std::optional<D> last_;
  };

  Local3StateExchange<Slice> exchange_;
};

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_TWO_THREAD_CONCURRENT_H
