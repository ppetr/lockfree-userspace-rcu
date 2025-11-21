// Copyright 2022-2023 Google LLC
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

  static_assert(std::is_copy_constructible<MutableT>::value &&
                    std::is_copy_assignable<MutableT>::value,
                "T must be copy constructible and assignable");

  class View;

  template <typename U = T>
  class SnapshotDeleter {
   public:
    SnapshotDeleter(const SnapshotDeleter &) noexcept = default;
    SnapshotDeleter &operator=(const SnapshotDeleter &) noexcept = default;

    void operator()(U *) { registrar_.snapshot_depth_--; }

   private:
    SnapshotDeleter(View &registrar) noexcept : registrar_(registrar) {}

    View &registrar_;

    friend class View;
  };

  // Holds a read reference to a RCU value for the current thread.
  // The reference is guaranteed to be stable during the lifetime of `Snapshot`.
  // Callers are expected to limit the lifetime of `Snapshot` to as short as
  // possible.
  // WARNING: Bad things will happen if you use `reset` on a `Snapshot`.
  // Thread-compatible (but not thread-safe), reentrant.
  using Snapshot = std::unique_ptr<T, SnapshotDeleter<>>;

  // Interface to the RCU local to a particular reader thread.
  // Construction and destruction are thread-safe operations, but the `Read()`
  // (and `ReadPtr()`) methods are only thread-compatible. Callers are expected
  // to construct a separate `View` instance for each reader thread.
  class View final {
   private:
    struct PrivateConstructed {};

   public:
    // Thread-safe. Argument `rcu` must outlive this instance.
    // Acquires a mutex to register in `rcu`, therefore it'll block waiting on
    // the mutex if a concurrent call to `Update` is running.
    View(PrivateConstructed, CopyRcu &rcu)
        : snapshot_depth_(0), local_rcu_(rcu.Current()) {}

    // Obtains a read snapshot to the current value held by the RCU.
    // Never returns `nullptr`.
    // This is a very fast, lock-free and atomic operation.
    // Thread-compatible, but not thread-safe.
    //
    // Reentrancy: Each call to `Read()` increments an internal reference
    // counter, which is decremented by releasing a `Snapshot`. Only when the
    // counter is being incremented from 0 a fresh value is obtained from the
    // RCU. Subsequent nested calls to `Read()` return the same value. This
    // mechanism ensures that the value of a `Snapshot` is not changed by such
    // nested calls.
    //
    // WARNING: Do not use `reset` or `release` on the returned `unique_ptr`.
    // Doing so is likely to lead to undefined behavior.
    inline Snapshot Read() noexcept {
      if (snapshot_depth_++ == 0) {
        local_rcu_.TryRead();
      }
      return Snapshot(&local_rcu_.Read(), SnapshotDeleter<>(*this));
    }

    // In case `T` is a `std::shared_ptr`, `ReadPtr` provides convenient access
    // directly to the pointer's target.
    //
    // Note that when using `ReadPtr` the `shared_ptr` is never destroyed by
    // the calling thread, thus avoiding any performance penalty related to its
    // internal reference counting or invoking `~T`. Rather the pointer is
    // destroyed by the updater thread during one of the subsequent `Update`
    // passes.
    template <typename U = T, typename E = typename U::element_type>
    std::unique_ptr<E, SnapshotDeleter<E>> inline ReadPtr() noexcept {
      Snapshot snapshot = Read();
      if (*snapshot == nullptr) {
        // For a `nullptr` there is no value to keep. Therefore we can just let
        // `snapshot` get deleted and create a `nullptr`, which won't invoke
        // its deleter.
        return {nullptr, SnapshotDeleter<E>(*this)};
      } else {
        return {snapshot.release()->get(), SnapshotDeleter<E>(*this)};
      }
    }

   private:
    // Holds a `Local3StateRcu<MutableT>` and maintains its registration in
    // `CopyRcu`.

    // Incremented with each `Snapshot` instance. Ensures that `TryRead` is
    // invoked only for the outermost `Snapshot`, keeping its value unchanged
    // for its whole lifetime.
    int_fast16_t snapshot_depth_;
    Local3StateRcu<MutableT> local_rcu_;

    friend class CopyRcu;
  };

  // Constructs a RCU with an initial value `T()`.
  CopyRcu() : CopyRcu(T()) {}
  explicit CopyRcu(T initial_value)
      : lock_(), views_(std::move(initial_value)) {}

  // Updates `value` in all registered `View` threads.
  // Returns the previous value. Note that the previous value can still be
  // observed by readers that haven't obtained a fresh `Snapshot` instance yet.
  //
  // Thread-safe. This method isn't tied in any particular way to a `View`
  // instance corresponding to the current thread, and can be called also by
  // threads that have no `View` instance at all.
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
    if (pred(*views_.shared())) {
      return absl::make_optional(UpdateLocked(std::move(value)));
    }
    return absl::nullopt;
  }

  inline View &ThreadLocalView() noexcept {
    return views_
        .try_emplace(typename View::PrivateConstructed{}, std::ref(*this))
        .first;
  }

  // A variant of `View::Read()` that automatically maintains a `thread_local`
  // instance of `View` bound to `this`.
  //
  // This makes this function easier to use compared to an explicit management
  // of `View`, at the cost of some small inherent performance overhead of
  // `thread_local`.
  inline Snapshot Read() noexcept { return ThreadLocalView().Read(); }

  // A variant of `View::ReadPtr()` that automatically maintains a
  // `thread_local` instance of `View` bound to `this`.
  //
  // This makes this function easier to use compared to an explicit management
  // of `View`, at the cost of some inherent performance overhead of
  // `thread_local`.
  template <typename U = T, typename E = typename U::element_type>
  inline std::unique_ptr<E, typename CopyRcu<U>::template SnapshotDeleter<E>>
  ReadPtr() noexcept {
    return ThreadLocalView().ReadPtr();
  }

 private:
  T UpdateLocked(typename std::remove_const<T>::type value)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    for (std::shared_ptr<View> &view : views_.Prune()) {
      Local3StateRcu<MutableT> &local_rcu = view->local_rcu_;
      local_rcu.Update() = value;
      local_rcu.ForceUpdate();
    }
    std::swap(*views_.shared(), value);
    return value;
  }

  ABSL_MUST_USE_RESULT T Current() ABSL_LOCKS_EXCLUDED(lock_) {
    absl::MutexLock mutex(&lock_);
    return *views_.shared();
  }

  absl::Mutex lock_;
  ThreadLocalWeak<View, MutableT> views_;
};

// By using `CopyRcu<shared_ptr<const T>>` we accomplish a RCU implementation
// with the common API
//
// - `Update` receives a pointer (`shared_ptr` or by conversion `unique_ptr`)
//   and shares it among all the `CopyRcu::View` receivers.
// - `ReadPtr` obtains a locally-scoped snapshot of `const T`.
//
// Note that no memory (de)allocation happens in the reader threads that invoke
// `ReadPtr` (or `Read`). This is done exclusively by the updater thread.
template <typename T>
using Rcu = CopyRcu<std::shared_ptr<typename std::add_const<T>::type>>;

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_COPY_RCU_H
