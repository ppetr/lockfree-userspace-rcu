// Copyright 2022-2025 Google LLC
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

#ifndef _SIMPLE_RCU_COPY_RCU_H
#define _SIMPLE_RCU_COPY_RCU_H

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"
#include "absl/log/absl_log.h"
#include "absl/memory/memory.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "simple_rcu/local_3state_rcu.h"
#include "simple_rcu/thread_local.h"

namespace simple_rcu {

// Generic, user-space RCU implementation with fast, atomic, lock-free reads.
//
// `T` must be copyable.
//
// Copies of objects of type `T` are distributed to thread-local receivers.
template <typename T>
class CopyRcu {
 public:
  using MutableT = typename std::remove_const<T>::type;

  // Provides a thread-local view of a `CopyRcu`
  class View {
   private:
    struct PrivateConstruction final {};

   public:
    explicit View(CopyRcu &rcu, PrivateConstruction = {})
        : local_(rcu.Current()) {}

    // Retrieves the most recent value and returns a reference to it. It allows
    // efficient access to the value in case `T` isn't cheaply copyable.
    // In addition, the second returned value signals whether this value is
    // new (observed the first time by the current thread).
    //
    // The returned reference is thread-local, valid only for the current
    // thread, and only until another call to any of the `Snapshot`... methods
    // by the current thread.
    inline std::pair<T &, bool> SnapshotRef() noexcept {
      bool is_new = local_.TryRead();
      return {local_.Read(), is_new};
    }

   private:
    Local3StateRcu<MutableT> local_;

    friend class CopyRcu;
  };

  static_assert(std::is_copy_constructible<MutableT>::value &&
                    std::is_copy_assignable<MutableT>::value,
                "T must be copy constructible and assignable");

  // Constructs a RCU with an initial value `T()`.
  CopyRcu() : CopyRcu(T()) {}
  explicit CopyRcu(T initial_value)
      : lock_(), current_(std::move(initial_value)), views_() {}

  // Updates the current `value`.  Returns the previous value.
  //
  // Thread-safe. This method isn't tied in any particular way to an instance
  // corresponding to the current thread, and can be called also by threads
  // that have no thread-local instance at all.
  T Update(typename std::remove_const<T>::type value)
      ABSL_LOCKS_EXCLUDED(lock_) {
    absl::MutexLock mutex(&lock_);
    return UpdateLocked(std::move(value));
  }
  // Similar to `Update`, but replaces the value only if the old one satisfies
  // the given predicate. Often the predicate will be an equality with a
  // previous value.
  absl::optional<T> UpdateIf(typename std::remove_const<T>::type value,
                             absl::FunctionRef<bool(const T &)> pred)
      ABSL_LOCKS_EXCLUDED(lock_) {
    absl::MutexLock mutex(&lock_);
    if (pred(current_)) {
      return absl::make_optional(UpdateLocked(std::move(value)));
    }
    return absl::nullopt;
  }

  // Fetches a copy of the latest value.
  // Thread-safe.
  inline T Snapshot() noexcept { return ThreadLocalView().SnapshotRef().first; }

  // Retrieves the thread-local `View` for the current thread.
  // This allows somewhat lower-level access than `Snapshot`, in particular to
  // call `View::SnapshotRef`, and by keeping the `View&` reference, some
  // (tiny) performance penalty related to fetching this thread-local `View&`
  // instance.
  //
  // Thread-safe.
  // The returned reference is valid only for the current thread.
  inline View &ThreadLocalView() noexcept {
    return views_
        .try_emplace(std::ref(*this), typename View::PrivateConstruction())
        .first;
  }

 private:
  T UpdateLocked(typename std::remove_const<T>::type value)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    for (std::shared_ptr<View> &ptr : views_.PruneAndList()) {
      Local3StateRcu<MutableT> &local = ptr->local_;
      local.Update() = value;
      local.ForceUpdate();
    }
    std::swap(current_, value);
    return value;
  }

  ABSL_MUST_USE_RESULT T Current() ABSL_LOCKS_EXCLUDED(lock_) {
    absl::MutexLock mutex(&lock_);
    return current_;
  }

  absl::Mutex lock_;
  MutableT current_ ABSL_GUARDED_BY(lock_);
  ThreadLocalWeak<View> views_;
};

// By using `CopyRcu<shared_ptr<const T>>` we accomplish a RCU implementation
// with the common API
//
// - `Update` receives a pointer (`shared_ptr` or by conversion `unique_ptr`)
//   and shares it among all the thread-local receivers.
template <typename T>
using Rcu = CopyRcu<std::shared_ptr<typename std::add_const<T>::type>>;

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_COPY_RCU_H
