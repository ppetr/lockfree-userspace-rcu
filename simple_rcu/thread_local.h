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
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "absl/log/die_if_null.h"

namespace simple_rcu {

class InternalPerThreadBase {
 private:
  ~InternalPerThreadBase() = default;

  struct MarkAbandoned {
    void operator()(InternalPerThreadBase *b) {
      b->held_.clear(std::memory_order_release);
    }
  };

  std::atomic_flag held_;
  bool abandoned_ = false;

  bool abandoned() {
    return abandoned_ || (!held_.test_and_set(std::memory_order_acquire) &&
                          (abandoned_ = true));
  }

  InternalPerThreadBase() { held_.test_and_set(); }

  // Keeps shared global objects as keys and values map into per-thread objects
  // owned by the global key.
  static absl::flat_hash_map<
      std::shared_ptr<void>,
      std::unique_ptr<InternalPerThreadBase, MarkAbandoned>> &
  Map();

  template <typename L, typename S>
  friend class ThreadLocal;
};

// Keeps shared global objects as keys and values map into per-thread objects
// owned by the global key.
absl::flat_hash_map<void *, std::shared_ptr<void>> &
InternalThreadLocalOwnedMap();

// Lock-free thread-local variables of type `L` that are identified by a shared
// state of `shared_ptr<S>::get()`.
//
// Note that there is usually a considerable performance penalty involved with
// `thread_local` variables, which is the bottle-neck of this class.
template <typename L, typename S>
class ThreadLocal {
 public:
  struct PerThread : public InternalPerThreadBase {
   public:
    L value;

    template <typename... Args>
    explicit PerThread(Args... args_) : value(std::forward<Args>(args_)...) {}
  };
  struct PruneResult {
    std::vector<std::shared_ptr<L>> current;
    std::vector<std::unique_ptr<PerThread>> abandoned;
  };

  template <typename... Args>
  explicit ThreadLocal(Args... args_)
      : shared_(std::make_shared<Shared>(std::forward<Args>(args_)...)) {}
  ~ThreadLocal() = default;

  std::shared_ptr<S> shared() const noexcept {
    return std::shared_ptr<S>(shared_, shared_->value);
  }

  // Retrieves a thread-local instance bound to `shared`.
  //
  // The return value semantic is equivalent to the common `try_emplace`
  // function: Iff there is no thread-local value available yet, it is
  // constructed from `args` and `true` is returned in the 2nd part of the
  // result.
  //
  // The returned `ThreadLocal::shared()` is set to the `shared` argument.
  template <typename... Args>
  inline std::pair<L &, bool> try_emplace(Args... args) {
    auto &map_ptr = InternalPerThreadBase::Map()[ABSL_DIE_IF_NULL(shared_)];
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

  PruneResult Prune() {
    PruneResult result;
    absl::MutexLock mutex(&shared_->per_thread_lock);
    std::vector<std::unique_ptr<PerThread>> &per_thread = shared_->per_thread;
    per_thread.shrink_to_fit();
    auto it = std::partition(
        per_thread.begin(), per_thread.end(),
        [](const std::unique_ptr<PerThread> &i) { return !i->abandoned(); });
    // Move abandoned instances to the result.
    result.abandoned.reserve(per_thread.end() - it);
    result.abandoned.insert(result.abandoned.end(), std::make_move_iterator(it),
                            std::make_move_iterator(per_thread.end()));
    per_thread.erase(it, per_thread.end());
    // Copy pointers to the remaining ones.
    result.current.reserve(shared_->per_thread.size());
    for (const auto &r : per_thread) {
      result.current.push_back(std::shared_ptr<L>(shared_, &r->value));
    }
    return result;
  }

 private:
  struct Shared {
    S value;

    template <typename... Args>
    explicit Shared(Args... args_) : value(std::forward<Args>(args_)...) {}

    absl::Mutex per_thread_lock;
    std::vector<std::unique_ptr<PerThread>> per_thread
        ABSL_GUARDED_BY(per_thread_lock);
  };

  // Never nullptr (unless moved out).
  std::shared_ptr<Shared> shared_;
};

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_THREAD_LOCAL_H
