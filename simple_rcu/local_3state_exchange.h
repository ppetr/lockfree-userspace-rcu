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
 private:
  using Index = AtomicSignedLockFree;
  static_assert(!std::is_void_v<Index> &&
                    std::atomic<Index>::is_always_lock_free,
                "Not lock-free on this architecture, please report this as a "
                "bug on the project's GitHub page");

  constexpr static Index kIndexMask = 3;
  constexpr static Index kRightMask = 4;
  constexpr static Index kExchangedMask = 8;

  struct Context final {
    // Stores just the first `kIndexMask` bits.
    Index index;
    // Stored with all bits, exactly what has been written to the atomic
    // variable.
    Index last;
  };

 public:
  template <bool Right>
  class Side final {
   public:
    inline const T& ref() const { return main_.values_[context().index]; }
    inline T& ref() { return main_.values_[context().index]; }

    struct PassResult {
      T& ref;
      bool exchanged;
      bool past_exchanged;
    };

    inline PassResult Pass() {
      return Pass([](const T&) {});
    }

    // If it's possible that the result will have `past_exchanged` true,
    // callback `might_double_exchange` is guaranteed to be called on the
    // `ref()` value just before its passed on. However, its call will be
    // avoided, if its possible to infer `!might_double_exchange`.
    template <typename F>
    inline PassResult Pass(F&& might_double_exchange) {
      Context& ctx = context();
      bool called_might_double_exchange = (ctx.last & kExchangedMask) != 0;
      if (called_might_double_exchange) {
        std::forward<F>(might_double_exchange)(ref());
      }
      Index received = ctx.last;
      // TODO: new_index can be just ctx.last.
      Index new_index = ctx.index | (Right ? kRightMask : 0);
      const bool exchanged = !main_.passing_.compare_exchange_strong(
          received, new_index, std::memory_order_acq_rel);
      if (exchanged) {
        if (((received & kExchangedMask) != 0) &&
            !std::exchange(called_might_double_exchange, true)) {
          std::forward<F>(might_double_exchange)(ref());
        }
        new_index |= kExchangedMask;
        received =
            main_.passing_.exchange(new_index, std::memory_order_acq_rel);
      }
      ctx.last = new_index;
      ctx.index = received & kIndexMask;
      return PassResult{.ref = ref(),
                        .exchanged = exchanged,
                        .past_exchanged = (received & kExchangedMask) != 0};
    }

   private:
    using Index = Local3StateExchange::Index;

    inline explicit Side(Local3StateExchange& main) : main_(main) {}

    inline Context& context() { return main_.context_[Right]; }

    Local3StateExchange& main_;

    friend class Local3StateExchange;
  };

  Local3StateExchange()
      : passing_(1),
        context_{Context{.index = 0, .last = 1},
                 Context{.index = 2, .last = -1}} {}

  template <bool Right>
  Side<Right> side() {
    return Side<Right>(*this);
  }
  template <bool Right>
  const Side<Right> side() const {
    return Side<Right>(*this);
  }

 private:
  std::atomic<Index> passing_;
  std::array<Context, 2> context_;
  std::array<T, 3> values_{T{}, T{}, T{}};
};

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_3STATE_EXCHANGE_H
