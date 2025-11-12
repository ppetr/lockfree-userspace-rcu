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
#include <type_traits>

#ifndef _SIMPLE_RCU_LOCK_FREE_INT_H
#define _SIMPLE_RCU_LOCK_FREE_INT_H

namespace simple_rcu {

// Finds the first of a given list of `U` types for which
// `std::atomic<U>::is_always_lock_free` and expose it in a `type` type member.
// If no `U` matches, `using type = void`.
template <class... Us>
struct FindAlwaysLockFree;

template <class U, class... Us>
struct FindAlwaysLockFree<U, Us...> {
  using type =
      typename std::conditional_t<std::atomic<U>::is_always_lock_free, U,
                                  typename FindAlwaysLockFree<Us...>::type>;
};

template <>
struct FindAlwaysLockFree<> {
  using type = void;
};

using IntLockFree64 =
    FindAlwaysLockFree<int_fast64_t, int64_t, long long int>::type;
using IntLockFree32 =
    FindAlwaysLockFree<int_fast32_t, IntLockFree64, int32_t, long int>::type;
using IntLockFree16 = FindAlwaysLockFree<int_fast16_t, IntLockFree32, int16_t,
                                         short int, int>::type;
using IntLockFree8 =
    FindAlwaysLockFree<int_fast8_t, IntLockFree16, int8_t>::type;

#ifdef __cpp_lib_atomic_lock_free_type_aliases
using AtomicSignedLockFree = typename std::atomic_signed_lock_free::value_type;
#else
using AtomicSignedLockFree = IntLockFree8;
#endif

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_LOCK_FREE_INT_H
