// Copyright 2026 Google LLC
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

#ifndef _SIMPLE_RCU_BI_DI_CHANNEL_H
#define _SIMPLE_RCU_BI_DI_CHANNEL_H

#include <utility>
#include <variant>

#include "simple_rcu/two_thread_concurrent.h"

namespace simple_rcu {

struct Monoid {
  struct MonoState : public std::monostate {
    inline MonoState& operator+=(const MonoState&) { return *this; }
  };

  template <typename C>
  struct Free {
    inline Free& operator+=(typename C::value_type element) {
      collection.push_back(std::move(element));
      return *this;
    }

    Free& operator+=(C from) {
      collection.insert(collection.end(), std::make_move_iterator(from.begin()),
                        std::make_move_iterator(from.end()));
      return *this;
    }

    C collection;
  };

  Monoid() = delete;
};

template <typename M, typename O, typename MOp = M, typename OOp = O>
class BiDiChannel {
 public:
  BiDiChannel() = default;

  inline O UpdateLeft(MOp diff) {
    return GetOrZero<1>(
        ttc_.template Update<false>(D(std::in_place_index<1>, std::move(diff)))
            .first);
  }

  inline M UpdateRight(OOp diff) {
    return GetOrZero<0>(
        ttc_.template Update<true>(D(std::in_place_index<2>, std::move(diff)))
            .first);
  }

 private:
  struct NoOp : public std::monostate {};
  using C = std::variant<M, O>;
  // `NoOp` must be first to default-initialize to it.
  using D = std::variant<NoOp, MOp, OOp>;

  struct Merge {
   public:
    static constexpr inline D NoOp() { return D(std::in_place_index<0>); }

    static inline void Apply(C& target, D diff) {
      if (MOp* ptr = std::get_if<1>(&diff); ptr != nullptr) {
        AddOrGet<0>(target, std::move(*ptr));
      } else if (OOp* ptr = std::get_if<2>(&diff); ptr != nullptr) {
        AddOrGet<1>(target, std::move(*ptr));
      }
    }

   private:
    template <size_t N>
    inline static void AddOrGet(C& target,
                                std::variant_alternative_t<N + 1, D>&& diff) {
      if (auto* ptr = std::get_if<N>(&target); ptr != nullptr) {
        *ptr += std::move(diff);
      } else {
        target.template emplace<N>() += std::move(diff);
      }
    }
  };

  template <size_t N>
  inline std::variant_alternative_t<N, C> GetOrZero(const C& v) {
    if (const auto* ptr = std::get_if<N>(&v); ptr != nullptr) {
      // The value will be destroyed by applying the opposite operation over
      // it, so it's fine to move it out.
      return std::move(const_cast<std::variant_alternative_t<N, C>&>(*ptr));
    } else {
      return {};
    }
  }

  TwoThreadConcurrent<C, D, Merge> ttc_;
};

template <typename M, typename MOp>
class BiDiChannel<M, Monoid::MonoState, MOp, Monoid::MonoState> {
 public:
  BiDiChannel() = default;

  inline Monoid::MonoState UpdateLeft(MOp diff) {
    ttc_.template Update<false>(std::move(diff));
    return {};
  }

  inline M UpdateRight(Monoid::MonoState = {}) {
    // The value will be destroyed by applying `MonoState` over it, so it's
    // fine to move it out.
    return std::move(
        const_cast<M&>(ttc_.template Update<true>(Monoid::MonoState()).first));
  }

 private:
  // `MonoState` must be first to default-initialize to it.
  using D = std::variant<Monoid::MonoState, MOp>;

  struct Merge {
    // We abuse the `MonoState` here a bit: Since we never use
    // `TwoThreadConcurrent::ObserveLast()`, we also use it for `NoOp` just for
    // the initial state, even though it does modify `M`.
    static constexpr inline D NoOp() { return D(std::in_place_index<0>); }

    static inline void Apply(M& target, D diff) {
      if (MOp* ptr = std::get_if<1>(&diff); ptr != nullptr) {
        target += std::move(*ptr);
      } else {
        target = M{};  // MonoState
      }
    }
  };

  TwoThreadConcurrent<M, D, Merge> ttc_;
};

template <typename M, typename MOp = M>
using UniDiChannel = BiDiChannel<M, Monoid::MonoState, MOp>;

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_BI_DI_CHANNEL_H
