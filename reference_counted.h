// Copyright 2020 Google LLC
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

#ifndef _REFCOUNT_H
#define _REFCOUNT_H

#include <atomic>
#include <cassert>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"

namespace refptr {

class Refcount {
 public:
  constexpr Refcount() : count_{1} {}

  // Increments the reference count. Imposes no memory ordering.
  inline void Inc() {
    // No need for any synchronization/ordering, as the value is not inspected
    // at all.
    count_.fetch_add(1, std::memory_order_relaxed);
  }

  // Returns whether the atomic integer is 1.
  inline bool IsOne() const {
    // This thread must observe the correct value, including any prior
    // modifications by other threads.
    return count_.load(std::memory_order_acquire) == 1;
  }

  // Returns `true` iff the counter's value is zero after the decrement
  // operation. In such a case the caller must destroy the referenced object,
  // and the counter's state becomes undefined.
  //
  // A caller should pass `expect_one = true` if there is a reasonable chance
  // that there is only a single reference to the object. This allows slight
  // performane optimization when reqesting the appropriate memory barriers.
  inline bool Dec(bool expect_one = false) {
    // This thread must observe the correct value if `refcount` reaches zero,
    // including any prior modifications by other threads. All other threads
    // must observe the result of the operation.
    if (expect_one && IsOne()) {
      // Knowing the object will be destructed, we don't decrement the counter.
      // This way, we save the _release operation_ that would be needed for
      // decrementing it below.
      return true;
    }
    int32_t refcount = count_.fetch_sub(1, std::memory_order_acq_rel);
    assert(refcount > 0);
    return refcount == 1;
  }

 private:
  std::atomic<int32_t> count_;
};

// Keeps a `Refcount`-ed instance of `T`.
//
// When a caller requests deletion of an instance via `SelfDelete`, `Alloc`
// is used to destroy and delete the memory block.
template <typename T, class Alloc = std::allocator<T>>
struct Refcounted {
 public:
  template <typename... Arg>
  ABSL_DEPRECATED(
      "Do not use - use `New` below instead. The constructor is made public "
      "just so that it's possible to use `construct` of an allocator to "
      "construct new instances.")
  Refcounted(Alloc allocator_, Arg&&... args_)
      : allocator(std::move(allocator_)),
        refcount(),
        nested(std::forward<Arg>(args_)...) {}

  template <typename... Arg>
  static Refcounted* New(Alloc allocator_, Arg&&... args_) {
    SelfAlloc self_allocator(std::move(allocator_));
    Refcounted* ptr =
        std::allocator_traits<SelfAlloc>::allocate(self_allocator, 1);
    std::allocator_traits<SelfAlloc>::construct(
        self_allocator, ptr, self_allocator, std::forward<Arg>(args_)...);
    return ptr;
  }

  void SelfDelete() && {
    // Move out the allocator to a local variable so that `this` can be
    // destroyed.
    auto allocator_copy = std::move(allocator);
    std::allocator_traits<SelfAlloc>::destroy(allocator_copy, this);
    std::allocator_traits<SelfAlloc>::deallocate(allocator_copy, this, 1);
  }

  mutable Refcount refcount;
  T nested;

 private:
  using SelfAlloc =
      typename std::allocator_traits<Alloc>::template rebind_alloc<Refcounted>;

  SelfAlloc allocator;
};

}  // namespace refptr

#endif  // _REFCOUNT_H
