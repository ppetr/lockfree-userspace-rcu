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

#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/memory/memory.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "simple_rcu/local_3state_rcu.h"

namespace simple_rcu {

// Generic, user-space RCU implementation with fast, atomic, lock-free reads.
//
// `T` must be copyable.
//
// Copies of objects of type `T` are distributed to thread-local receivers.
template <typename T>
class CopyRcu {
 public:
  class Local;
  using MutableT = typename std::remove_const<T>::type;

  static_assert(std::is_default_constructible<MutableT>::value,
                "T must be default constructible");
  static_assert(std::is_copy_constructible<MutableT>::value &&
                    std::is_copy_assignable<MutableT>::value,
                "T must be copy constructible and assignable");

  template <typename U = T>
  class SnapshotDeleter {
   public:
    SnapshotDeleter(const SnapshotDeleter &) noexcept = default;
    SnapshotDeleter &operator=(const SnapshotDeleter &) noexcept = default;

    void operator()(U *) { registrar_.snapshot_depth_--; }

   private:
    SnapshotDeleter(Local &registrar) noexcept : registrar_(registrar) {}

    Local &registrar_;

    friend class Local;
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
  // to construct a separate `Local` instance for each reader thread.
  class Local final {
   public:
    // Thread-safe. Argument `rcu` must outlive this instance.
    // Acquires a mutex to register (and dereginster in `~Local`) in `rcu`,
    // therefore it'll block waiting on the mutex if a concurrent call to
    // `Update` is running.
    Local(CopyRcu &rcu) : rcu_(&rcu), snapshot_depth_(0), local_rcu_() {
      rcu.Register(*this);
    }
    Local(std::shared_ptr<CopyRcu> rcu)
        : rcu_(rcu), snapshot_depth_(0), local_rcu_() {
      rcu->Register(*this);
    }
    ~Local() {
      CopyRcu *const *ptr = absl::get_if<0>(&rcu_);
      if (ptr != nullptr) {
        (*ptr)->Unregister(*this);
      } else {
        const std::shared_ptr<CopyRcu> rcu_ptr = absl::get<1>(rcu_).lock();
        if (rcu_ptr != nullptr) {
          rcu_ptr->Unregister(*this);
        }
      }
    }

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
    Snapshot Read() noexcept {
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
    template <typename U = T>
    std::unique_ptr<typename U::element_type,
                    SnapshotDeleter<typename U::element_type>>
    ReadPtr() noexcept {
      Snapshot snapshot = Read();
      if (*snapshot == nullptr) {
        // For a `nullptr` there is no value to keep. Therefore we can just let
        // `snapshot` get deleted and create a `nullptr`, which won't invoke
        // its deleter.
        return {nullptr, SnapshotDeleter<typename U::element_type>(*this)};
      } else {
        return {snapshot.release()->get(),
                SnapshotDeleter<typename U::element_type>(*this)};
      }
    }

   private:
    // Thread-compatible.
    MutableT Update(MutableT value) {
      local_rcu_.Update() = std::move(value);
      local_rcu_.ForceUpdate();
      return std::move(local_rcu_.Update());
    }

    const absl::variant<CopyRcu *, const std::weak_ptr<CopyRcu>> rcu_;
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
      : lock_(), value_(std::move(initial_value)), threads_() {}

  // Updates `value` in all registered `Local` threads.
  // Returns the previous value. Note that the previous value can still be
  // observed by readers that haven't obtained a fresh `Snapshot` instance yet.
  //
  // Thread-safe. This method isn't tied in any particular way to a `Local`
  // instance corresponding to the current thread, and can be called also by
  // threads that have no `Local` instance at all.
  T Update(typename std::remove_const<T>::type value) LOCKS_EXCLUDED(lock_) {
    absl::MutexLock mutex(&lock_);
    return UpdateLocked(std::move(value));
  }
  // Similar to `Update`, but replaces the value only if the old one satisfies
  // the given predicate. Often the predicate will be an equality with a
  // previous value.
  absl::optional<T> UpdateIf(typename std::remove_const<T>::type value,
                             absl::FunctionRef<bool(const T &)> pred)
      LOCKS_EXCLUDED(lock_) {
    absl::MutexLock mutex(&lock_);
    if (pred(value_)) {
      return absl::make_optional(UpdateLocked(std::move(value)));
    } else {
      return absl::nullopt;
    }
  }

  // Retrieves a thread-local instalce of `Local` bound to `rcu`.
  // It keeps a `std::weak_ptr` to `rcu` so that it unregisters from it if (and
  // only if) `rcu` is still alive when this thread is destroyed.
  static Local &GetThreadLocal(std::shared_ptr<CopyRcu> rcu) noexcept {
    static thread_local absl::flat_hash_map<CopyRcu *, std::unique_ptr<Local>>
        local_map;
    std::unique_ptr<Local> &local = local_map[rcu.get()];
    if (ABSL_PREDICT_FALSE(local == nullptr)) {
      local = absl::make_unique<Local>(std::move(rcu));
    }
    return *local;
  }

 private:
  T UpdateLocked(typename std::remove_const<T>::type value)
      EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    for (Local *thread : threads_) {
      thread->Update(value);
    }
    std::swap(value_, value);
    return value;
  }

  void Register(Local &thread) LOCKS_EXCLUDED(lock_) {
    absl::MutexLock mutex(&lock_);
    threads_.insert(&thread);
    thread.Update(value_);
  }

  void Unregister(Local &thread) LOCKS_EXCLUDED(lock_) {
    absl::MutexLock mutex(&lock_);
    threads_.erase(&thread);
  }

  absl::Mutex lock_;
  // The current value that has been distributed to all thread-`Local`
  // instances.
  MutableT value_ GUARDED_BY(lock_);
  // List of registered thread-`Local` instances.
  absl::flat_hash_set<Local *> threads_ GUARDED_BY(lock_);
};

// A variant of `CopyRcu<T>::Local::Read()` that automatically maintains a
// `thread_local` instance of `CopyRcu<T>::Local` bound to `rcu`.
//
// This makes this function easier to use compared to an explicit management of
// `Local`, at the cost of some inherentt performance overhead of
// `thread_local`.
template <typename T>
inline typename CopyRcu<T>::Snapshot Read(
    std::shared_ptr<CopyRcu<T>> rcu) noexcept {
  return CopyRcu<T>::GetThreadLocal(std::move(rcu)).Read();
}

// A variant of `CopyRcu<T>::Local::ReadPtr()` that automatically maintains a
// `thread_local` instance of `CopyRcu<T>::Local` bound to `rcu`.
//
// This makes this function easier to use compared to an explicit management of
// `Local`, at the cost of some inherentt performance overhead of
// `thread_local`.
template <typename T>
inline std::unique_ptr<
    typename T::element_type,
    typename CopyRcu<T>::template SnapshotDeleter<typename T::element_type>>
ReadPtr(std::shared_ptr<CopyRcu<T>> rcu) noexcept {
  return CopyRcu<T>::GetThreadLocal(std::move(rcu)).template ReadPtr<T>();
}

// By using `CopyRcu<shared_ptr<const T>>` we accomplish a RCU implementation
// with the common API
//
// - `Update` receives a pointer (`shared_ptr` or by conversion `unique_ptr`)
//   and shares it among all the `CopyRcu::Local` receivers.
// - `ReadPtr` obtains a locally-scoped snapshot of `const T`.
//
// Note that no memory (de)allocation happens in the reader threads that invoke
// `ReadPtr` (or `Read`). This is done exclusively by the updater thread.
template <typename T>
using Rcu = CopyRcu<std::shared_ptr<typename std::add_const<T>::type>>;

}  // namespace simple_rcu
