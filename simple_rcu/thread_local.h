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

#ifndef _SIMPLE_RCU_THREAD_LOCAL_H
#define _SIMPLE_RCU_THREAD_LOCAL_H

#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"

namespace simple_rcu {

// Lock-free thread-local variables of type `L` that are identified by a shared
// state of `shared_ptr<S>::get()`.
template <typename L, typename S>
class ThreadLocal {
 public:
  ~ThreadLocal() {
    S *ptr = shared_.get();
    if ((ptr != nullptr) &&
        std::weak_ptr<S>(std::exchange(shared_, nullptr)).expired()) {
      // This is the very last instance referencing the shared value.
      // Therefore we can clean the local now.
      auto &map = Map();
      auto it = map.find(ptr);
      ABSL_DCHECK(it != map.end());
      ABSL_DCHECK_EQ(&it->second.local, local_);
      map.erase(it);
    }
  }

  bool operator()() const noexcept { return local_ != nullptr; }

  // This reference is valid at least as long as the `shared_ptr` returned by
  // `shared()` is alive. Since this `ThreadLocal` keeps the pointer alive on
  // its own, the reference is valid for the lifetime of `this` (and possibly
  // longer).
  //
  // This reference is invalidated either by:
  // - Destroying this `ThreadLocal` object while it is the last owner of
  //   `shared()`. Or
  // - Calling `CleanUp` after the respective `shared_ptr` has been destroyed.
  L &local() const noexcept { return *local_; }

  // The shared instance `local()` is bound to.
  std::shared_ptr<S> shared() const noexcept { return shared_; }

  // Retrieves a thread-local instance bound to `shared`.
  //
  // The return value semantic is equivalent to the common `try_emplace`
  // function: Iff there is no thread-local value available yet, it is
  // constructed from `args` and `true` is returned in the 2nd part of the
  // result.
  //
  // The returned `ThreadLocal::shared()` is set to the `shared` argument.
  template <typename... Args>
  static std::pair<ThreadLocal, bool> try_emplace(std::shared_ptr<S> shared,
                                                  Args... args) noexcept {
    if (shared == nullptr) {
      return {ThreadLocal(nullptr, nullptr), false};
    }
    auto &local_map = Map();
    auto [it, inserted] = local_map.try_emplace(shared.get(), shared,
                                                std::forward<Args>(args)...);
    if (!inserted) {
      if (ABSL_PREDICT_FALSE(it->second.shared.expired())) {
        ABSL_DLOG(INFO) << "Corner-case: An expired pointer that happens to "
                           "have the same S* = "
                        << shared.get()
                        << " key (re-using the same memory location)";
        it->second = Stored(shared, std::forward<Args>(args)...);
        inserted = true;
      } else {
        ABSL_DCHECK_EQ(it->second.shared.lock().get(), shared.get());
      }
    }
    return {ThreadLocal(&it->second.local, std::move(shared)), inserted};
  }

  // Cleans up `L` instances created by `try_emplace`, whose shared state
  // objects have been deleted (as determined by their internal
  // `std::weak_ptr<S>`).
  // Returns the number of deleted objects.
  static int CleanUp() noexcept {
    int deleted_count = 0;
    auto &map = Map();
    for (auto it = map.begin(); it != map.end();) {
      if (it->second.shared.expired()) {
        map.erase(it++);
        deleted_count++;
      } else {
        it++;
      }
    }
    return deleted_count;
  }

 private:
  struct Stored {
   public:
    std::weak_ptr<S> shared;
    L local;

    template <typename... Args>
    Stored(std::shared_ptr<S> shared_, Args... args_)
        : shared(std::move(shared_)), local(std::forward<Args>(args_)...) {}

   private:
    friend class ThreadLocal;
  };

  ThreadLocal(L *local, std::shared_ptr<S> shared)
      : local_(local), shared_(std::move(shared)) {}

  static absl::flat_hash_map<S *, Stored> &Map() {
    static thread_local absl::flat_hash_map<S *, Stored> local_map;
    return local_map;
  }

  L *local_;
  std::shared_ptr<S> shared_;
};

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_THREAD_LOCAL_H
