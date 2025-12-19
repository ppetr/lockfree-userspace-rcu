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
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/function_ref.h"
#include "absl/synchronization/mutex.h"
#include "simple_rcu/local_3state_rcu.h"
#include "simple_rcu/thread_local.h"

namespace simple_rcu {

// Generic, user-space RCU implementation with fast, atomic, lock- (wait-)free
// reads.
//
// `T` must be copyable.
//
// As soon as a call to `Update` finishes, any thread that calls `Snapshot`
// will observe the value.
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
  //
  // Thread-safe.
  T Update(typename std::remove_const<T>::type value)
      ABSL_LOCKS_EXCLUDED(lock_) {
    std::lock_guard mutex(lock_);
    return UpdateLocked(std::move(value));
  }
  // Similar to `Update`, but replaces the value only if the old one satisfies
  // the given predicate. Often the predicate will be an equality with a
  // previous value.
  std::optional<T> UpdateIf(typename std::remove_const<T>::type value,
                            absl::FunctionRef<bool(const T &)> pred)
      ABSL_LOCKS_EXCLUDED(lock_) {
    std::lock_guard mutex(lock_);
    if (pred(current_)) {
      return std::make_optional(UpdateLocked(std::move(value)));
    }
    return std::nullopt;
  }

  // Fetches a copy of the latest value passed to `Update`.
  //
  // The first call a thread calls this method might be slower in order to
  // construct a thread-local channel for it. All subsequent alls are very
  // fast.
  //
  // Thread-safe, wait-free.
  inline T Snapshot() noexcept { return ThreadLocalView().SnapshotRef().first; }

  // Retrieves the thread-local `View` for the current thread.
  //
  // This allows somewhat lower-level access using `View::SnapshotRef`.
  //
  // Furthermore, using `View` directly skips accessing the internal
  // `LockFreeMetric`'s `thread_local` variable, making updates truly wait-free
  // regardless of the `thread_local` implementation.
  //
  // The returned reference is valid only for the current thread.
  //
  // Thread-safe, wait-free.
  inline View &ThreadLocalView() noexcept {
    return views_
        .try_emplace(std::ref(*this), typename View::PrivateConstruction())
        .first;
  }

  // Frees up the thread_local resources for the current thread. Idempotent.
  // Invalidates any references obtained from `ThreadLocalView()`.
  // This is called automatically when the current thread exists, so usually
  // there is no need to call this method explicitly.
  void erase() noexcept { views_.erase(); }

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
    std::lock_guard mutex(lock_);
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
//   and shares it among all the (internally thread-local) receivers.
// - `Snapshot` returns the latest `shared_ptr` to any calling thread.
template <typename T>
using Rcu = CopyRcu<std::shared_ptr<typename std::add_const<T>::type>>;

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_COPY_RCU_H
