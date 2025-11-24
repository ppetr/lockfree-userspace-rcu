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

#ifndef _SIMPLE_RCU_THREAD_LOCAL_H
#define _SIMPLE_RCU_THREAD_LOCAL_H

#include <algorithm>
#include <atomic>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "absl/log/die_if_null.h"
#include "absl/synchronization/mutex.h"
#include "simple_rcu/lock_free_int.h"

namespace simple_rcu {

class InternalPerThreadBase {
 private:
  using AtomicBool = std::conditional<
      std::atomic<bool>::is_always_lock_free, bool,
      std::conditional<std::is_void_v<AtomicSignedLockFree>, bool,
                       AtomicSignedLockFree>::type>::type;

  struct MarkAbandoned final {
    void operator()(InternalPerThreadBase *b) {
      b->abandoned_.store(true, std::memory_order_release);
    }
  };

  InternalPerThreadBase() : abandoned_(false) {}
  ~InternalPerThreadBase() = default;

  inline bool abandoned() { return abandoned_.load(std::memory_order_acquire); }

  // Using `static thread_local` instances in the .cc module in the functions
  // below is much more performant then having them templated in the .h file.
  //
  // Using `shared_ptr`s as keys ensures that they remain unique as long as
  // they're used. Using just `void*` can lead to cases when a new pointer is
  // allocated at exactly the same address, then mapped to instances which
  // don't belong to it, and crashes.

  // Keeps shared global objects as keys and values map into per-thread objects
  // owned by the global key.
  static absl::flat_hash_map<
      std::shared_ptr<void>,
      std::unique_ptr<InternalPerThreadBase, MarkAbandoned>>
      &NonOwnedMap();

  // Keeps shared global objects as keys and values map into per-thread objects
  // owned by the global key.
  static absl::flat_hash_map<std::shared_ptr<void>, std::shared_ptr<void>>
      &OwnedMap();

  std::atomic<AtomicBool> abandoned_;

  template <typename L>
  friend class ThreadLocalDelayed;
  template <typename L>
  friend class ThreadLocalWeak;
};

// Fast, lock-free thread-local variables of type `L` bound to a central
// object. They're created on-demand by `try_emplace` and kept (at least) as
// long as the respective thread is running.
//
// This implementation is "delayed" in the sense that thread-local instances of
// `L` aren't destroyed by the respective threads when they finish. Instead,
// they're owned by the central `ThreadLocalDelayed` instance and such
// abandoned ones are extracted by calling by the `ThreadLocalDelayed::Prune`
// method.
//
// This allows (1) to asynchronously process any state left over there and
// (2) speeds up destruction of finishing threads.
template <typename L>
class ThreadLocalDelayed {
 public:
  struct PerThread final : public InternalPerThreadBase {
   public:
    L value;

    template <typename... Args>
    explicit PerThread(Args... args_) : value(std::forward<Args>(args_)...) {}
  };

  ThreadLocalDelayed() : shared_(std::make_shared<Shared>()) {}
  ~ThreadLocalDelayed() = default;

  // Retrieves a thread-local instance bound to `shared`.
  //
  // The return value semantic is equivalent to the common `try_emplace`
  // function: Iff there is no thread-local value available yet, it is
  // constructed from `args` and `true` is returned in the 2nd part of the
  // result.
  //
  // The returned `ThreadLocalDelayed::shared()` is set to the `shared`
  // argument.
  template <typename... Args>
  inline std::pair<L &, bool> try_emplace(Args... args) {
    auto &map_ptr =
        InternalPerThreadBase::NonOwnedMap()[ABSL_DIE_IF_NULL(shared_)];
    if (map_ptr == nullptr || map_ptr->abandoned()) {
      auto owned = std::make_unique<PerThread>(std::forward<Args>(args)...);
      PerThread *const ptr = owned.get();
      {
        absl::MutexLock mutex(&shared_->per_thread_lock);
        shared_->per_thread.push_back(std::move(owned));
      }
      map_ptr =
          std::unique_ptr<PerThread, InternalPerThreadBase::MarkAbandoned>(ptr);
      return {ptr->value, true};
    } else {
      PerThread *ptr = reinterpret_cast<PerThread *>(map_ptr.get());
      return {ptr->value, false};
    }
  }

  // See the `Prune` method below.
  struct PruneResult final {
    std::vector<L *> current;
    std::vector<std::unique_ptr<PerThread>> abandoned;
  };

  // Iterates through all `L` instances bound to this central one.
  // They are returned in the result, split into two part:
  //
  // - `current` contains the ones that are still referenced by their
  //   creator threads. These references are valid only until another call to
  //   `PruneAndList`, or until this central object is destroyed. The caller is
  //   responsible for any required synchronization of these values with
  //   respect to their creator threads.
  // - `abandoned` contains the ones whose respective thread has finished and
  //   therefore their ownership is handed over to the caller to destroy (or
  //   otherwise utilize) them.
  //
  // None of the returned pointers are `nullptr`.
  PruneResult PruneAndList() {
    PruneResult result;
    absl::MutexLock mutex(&shared_->per_thread_lock);
    std::vector<std::unique_ptr<PerThread>> &per_thread = shared_->per_thread;
    // Shrink-to-fit before removals as a small optimization - possibly the
    // roughly same number of new entries will be added until next call.
    per_thread.shrink_to_fit();
    auto it = std::partition(
        per_thread.begin(), per_thread.end(),
        [](const std::unique_ptr<PerThread> &i) { return !i->abandoned(); });
    // Move abandoned instances to the result.
    result.abandoned.reserve(per_thread.end() - it);
    result.abandoned.insert(result.abandoned.end(), std::make_move_iterator(it),
                            std::make_move_iterator(per_thread.end()));
    per_thread.erase(it, per_thread.end());
    // Copy pointers to the kept instances.
    result.current.reserve(per_thread.size());
    for (const auto &r : per_thread) {
      result.current.push_back(&r->value);
    }
    return result;
  }

 private:
  struct Shared final {
    absl::Mutex per_thread_lock;
    std::vector<std::unique_ptr<PerThread>> per_thread
        ABSL_GUARDED_BY(per_thread_lock);
  };

  // Never nullptr (unless moved out).
  std::shared_ptr<Shared> shared_;
};

// Fast, lock-free thread-local variables of type `L` bound to a central
// object. They're created on-demand by `try_emplace` and kept (at least) as
// kept (at least) as long as the respective thread is running.
//
// This implementation is "weak" in the sense that thread-local instances of
// `L` are only weakly referenced by the central class. When a thread finishes
// execution, its `ThreadLocalWeak` variables will be destroyed, unless they
// have been `lock()`-ed by a previously/concurrently running call to `Prune`.
template <typename L>
class ThreadLocalWeak {
 public:
  ThreadLocalWeak() : ThreadLocalWeak(nullptr) {}
  // `shared` will be kept alive as long as there are any thread-local
  // instances bound to this object.
  explicit ThreadLocalWeak(std::shared_ptr<void> shared)
      : shared_((shared != nullptr) ? std::move(shared)
                                    : std::make_shared<std::monostate>()) {}
  ~ThreadLocalWeak() = default;

  // Retrieves a thread-local instance bound to `shared`.
  //
  // The return value semantic is equivalent to the common `try_emplace`
  // function: Iff there is no thread-local value available yet, it is
  // constructed from `args` and `true` is returned in the 2nd part of the
  // result.
  //
  // The returned `ThreadLocalWeak::shared()` is set to the `shared` argument.
  template <typename... Args>
  inline std::pair<L &, bool> try_emplace(Args... args) {
    auto &map_ptr =
        InternalPerThreadBase::OwnedMap()[ABSL_DIE_IF_NULL(shared_)];
    if (map_ptr == nullptr) {
      auto owned = std::make_shared<PerThread>(std::forward<Args>(args)...);
      locals_.Add(owned);
      map_ptr = owned;
      return {*owned, true};
    } else {
      return {*reinterpret_cast<PerThread *>(map_ptr.get()), false};
    }
  }

  // Removes this thread's local `L` value. If it's not held by a
  // `shared_ptr<L>` returned by `PruneAndList`, it's destroyed immediately. In
  // either case, the original reference returned by `try_emplace` becomes
  // invalid. A new call to `try_emplace` will create and register a new,
  // different one.
  void erase() {
    auto &map = InternalPerThreadBase::OwnedMap();
    auto it = map.find(shared_);
    if (it != map.end()) {
      map.erase(it);
    }
  }

  // Cleans up all expired `weak-ptr`s from an internal bookkeeping list.
  // The ones that are still referenced are `weak_ptr::lock()`-ed and returned.
  //
  // The caller is responsible for any required synchronization of the returned
  // values with respect to the threads that created them.
  std::vector<std::shared_ptr<L>> PruneAndList() {
    return locals_.PruneAndList();
  }

  // Cleans up all expired `weak-ptr`s from an internal bookkeeping list.
  void PruneOnly() { return locals_.PruneOnly(); }

 private:
  using PerThread = L;

  class LocalsList final {
   public:
    void Add(std::weak_ptr<PerThread> ptr)
        ABSL_LOCKS_EXCLUDED(per_thread_lock_) {
      absl::MutexLock mutex(&per_thread_lock_);
      per_thread_.emplace_back(ptr);
    }

    std::vector<std::shared_ptr<L>> PruneAndList()
        ABSL_LOCKS_EXCLUDED(per_thread_lock_) {
      absl::MutexLock mutex(&per_thread_lock_);
      PruneInternal();
      std::vector<std::shared_ptr<L>> result;
      result.reserve(per_thread_.size());
      for (std::weak_ptr<PerThread> ptr : per_thread_) {
        if (std::shared_ptr<PerThread> locked = ptr.lock(); locked != nullptr) {
          result.push_back(std::move(locked));
        }
      }
      return result;
    }

    void PruneOnly() ABSL_LOCKS_EXCLUDED(per_thread_lock_) {
      absl::MutexLock mutex(&per_thread_lock_);
      PruneInternal();
    }

   private:
    void PruneInternal() ABSL_EXCLUSIVE_LOCKS_REQUIRED(per_thread_lock_) {
      // Shrink-to-fit before removals as a small optimization - possibly the
      // roughly same number of new entries will be added until the next call.
      per_thread_.shrink_to_fit();
      per_thread_.erase(std::remove_if(per_thread_.begin(), per_thread_.end(),
                                       [](auto &ptr) { return ptr.expired(); }),
                        per_thread_.end());
    }

    absl::Mutex per_thread_lock_;
    std::vector<std::weak_ptr<PerThread>> per_thread_
        ABSL_GUARDED_BY(per_thread_lock_);
  };

  // Never nullptr (unless moved out).
  std::shared_ptr<void> shared_;
  LocalsList locals_;
};

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_THREAD_LOCAL_H
