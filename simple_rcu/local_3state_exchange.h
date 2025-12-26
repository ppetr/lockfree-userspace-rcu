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
#include <new>  // std::hardware_destructive_interference_size
#include <utility>

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

#ifdef __cpp_lib_hardware_interference_size
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winterference-size"
  constexpr static size_t kPadding =
      std::hardware_destructive_interference_size;
#pragma GCC diagnostic pop
#else
  constexpr static size_t kPadding = 64;
#endif

  constexpr static Index kIndexMask = 3;
  constexpr static Index kRightMask = 4;
  constexpr static Index kExchangedMask = 8;

  struct alignas(kPadding) Slot final {
    template <typename... Args>
    explicit Slot(Args... args_) : value(std::forward<Args>(args_)...) {}

    T value;
  };

  struct alignas(kPadding) Context final {
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
    inline const T& ref() const { return main_.values_[context().index].value; }
    inline T& ref() { return main_.values_[context().index].value; }

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
    //
    // If switching sides, the value passed to the callback is an r-value (&&),
    // as in this case the current value will be obsolete compared to the new
    // one, and the `TwoThreadConcurrent` algorithm can take advantage of this.
    template <typename F>
    inline PassResult Pass(F&& might_double_exchange) {
      Context& ctx = context();
      bool called_might_double_exchange = (ctx.last & kExchangedMask) != 0;
      if (called_might_double_exchange) {
        // The last call this thread observed so far was an exchange. If there
        // was no call by the other thread since then (which we'll know only
        // after the following CaS call), the callback needs to be called (and
        // it has to be before the CaS call).
        std::forward<F>(might_double_exchange)(ref());
      }
      Index received = ctx.last;
      // TODO: new_index can be just ctx.last.
      Index new_index = ctx.index | (Right ? kRightMask : 0);
      const bool exchanged = !main_.passing_.compare_exchange_strong(
          received, new_index, std::memory_order_acq_rel);
      if (exchanged) {
        // At this point we're using just two bits of information from the
        // failed CaS instruction:
        // (1) That the value didn't match `ctx.last`. Since the other thread
        //     always sets the opposite `kRightMask` bit, this will
        //     consistently signal a change by the other thread regardless of
        //     other state changes (thus preventing the ABA problem).
        // (2) The `kExchangedMask` is used only opportunisticly for
        //     `might_double_exchange`. This bit is set by the first call by
        //     the other thread (which we know it has happend because of
        //     `exchange`) and unset for any subsequent calls. Therefore, if
        //     it's unset now, it'll be unset also after the following
        //     Swap call (`DCHECK`-ed).
        if (((received & kExchangedMask) != 0) &&
            !std::exchange(called_might_double_exchange, true)) {
          // When exchanging sides, the value passed to the other side will lag
          // compared to its last one. The callback can take advantage of this
          // by moving the value out.
          std::forward<F>(might_double_exchange)(std::move(ref()));
        }
        new_index |= kExchangedMask;
        received =
            main_.passing_.exchange(new_index, std::memory_order_acq_rel);
      }
      ctx.last = new_index;
      ctx.index = received & kIndexMask;
      ABSL_DCHECK(called_might_double_exchange ||
                  ((received & kExchangedMask) == 0))
          << "On returning `past_exchanged = true` the `might_double_exchange` "
             "callback must have been called";
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

  Local3StateExchange() : Local3StateExchange(std::in_place) {}
  explicit Local3StateExchange(const T& initial)
      : Local3StateExchange(std::in_place, initial) {}
  template <typename... Args>
  explicit Local3StateExchange(std::in_place_t, Args... args)
      : passing_(1),
        context_{Context{.index = 0, .last = 1},
                 Context{.index = 2, .last = -1}},
        values_{Slot(args...), Slot(args...), Slot(args...)} {}

  template <bool Right>
  Side<Right> side() {
    return Side<Right>(*this);
  }
  template <bool Right>
  const Side<Right> side() const {
    return Side<Right>(*this);
  }

 private:
  alignas(kPadding) std::atomic<Index> passing_;
  std::array<Context, 2> context_;
  std::array<Slot, 3> values_;
};

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_3STATE_EXCHANGE_H
