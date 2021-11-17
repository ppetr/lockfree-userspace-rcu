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

template <typename T>
struct DefaultRefDeleter;

// Keeps a `Refcount`-ed instance of `T`.
//
// `Deleter` must be a copyable or moveable type with method
// `Delete(Refcounted<T, Deleter>*)` and must not be `final`.
// `Refcounted` internally derives from `Deleter` to achieve empty base
// optimization <https://en.cppreference.com/w/cpp/language/ebo>. When a caller
// requests its deletion via `SelfDelete`, `Deleter` is moved or copied out and
// then invoked on `this`.
template <typename T, class Deleter = DefaultRefDeleter<T>>
struct Refcounted : private Deleter {
  static_assert(std::is_copy_constructible<Deleter>::value ||
                    std::is_move_constructible<Deleter>::value,
                "The Deleter must be copy-constructible or move-constructible");
#if __cplusplus >= 201402L
  static_assert(!std::is_final<Deleter>::value,
                "The Deleter must not be `final`");
#endif  // __cplusplus >= 201703L

 public:
  template <typename... Arg>
  Refcounted(Deleter deleter, Arg&&... args)
      : Deleter(std::move(deleter)),
        refcount(),
        nested(std::forward<Arg>(args)...) {}

  void SelfDelete() && {
    // Move/copy out the deleter to a local variable so that `this` can be
    // destroyed.
    Deleter deleter = std::move(*this);
    (void)deleter.Delete(this);
  }

  mutable Refcount refcount;
  T nested;
};

template <typename T>
struct DefaultRefDeleter {
  void Delete(Refcounted<T, DefaultRefDeleter<T>>* to_delete) {
    delete to_delete;
  }
};

}  // namespace refptr

#endif  // _REFCOUNT_H
