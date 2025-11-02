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
#include <deque>
#include <optional>
#include <type_traits>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "simple_rcu/local_3state_rcu.h"

namespace simple_rcu {

template <typename T>
class LocalLockFreeMetric {
 public:
  const T& Update(absl::FunctionRef<void(T&) const> f) {
    bool collected;
    {
      Value& old = rcu_.Update();
      f(old.primary);
      LOG(INFO) << "Passing update value: " << old.primary << "@"
                << int{old.version};
      collected = rcu_.ForceUpdate();
    }
    Value& owned = rcu_.Update();
    if (owned.version > update_version_) {
      DCHECK(collected);
      LOG(INFO) << "Received a newer version from the collect thread: "
                << owned.primary << "@" << int{owned.version};
      copy_from_collect_ = owned.primary;
      update_version_ = owned.version;
    } else if (owned.version < update_version_) {
      LOG(INFO) << "Updating an old value that was " << owned.primary << "@"
                << int{owned.version};
      owned.primary = copy_from_collect_;
      owned.version = update_version_;
    }
    f(owned.primary);
    LOG(INFO) << "Current value: " << owned.primary << "@"
              << int{owned.version};
    // Invariant: `owned` holds a value at `update_version_`.
    return owned.primary;
    /*
    Value* const reclaiming = rcu_.ReclaimByUpdate();
    {
      Value& old = rcu_.Update();
      if (reclaiming != nullptr) {
        // The reclaimed value transfers a copy of its `primary` created by
        // the `Collect` method to avoid copying `T` while updating.
        std::swap(old.primary, reclaiming->collect_copy);
      }
      f(old.primary);
      if (rcu_.ForceUpdate() && (reclaiming == nullptr)) {
        // The value in `old.primary` was passed already to the collector,
        // therefore we must not update the currently owned, returned value.
        return rcu_.Update().primary;
      }
      if (reclaiming != nullptr) {
        DCHECK_EQ(&rcu_.Update(), reclaiming);
      }
    }
    Value& owned = rcu_.Update();
    f(owned.primary);
    return owned.primary;
    */
  }

  template <typename R>
  std::optional<R> Collect(absl::FunctionRef<R(T&)> f) {
    if (rcu_.TryRead()) {
      if (Value& owned = rcu_.Read(); owned.version == collect_version_) {
        collect_version_ = ++owned.version;
        LOG(INFO) << "Collected value " << owned.primary << "@"
                  << int{owned.version};
        return std::make_optional<R>(f(owned.primary));
      }
    }
    LOG(INFO) << "No value to collect";
    return std::nullopt;
    /*
    // Prepare a copy for the updater, and also destroy any previous value.
    owned.collect_copy = owned.primary;
    return result;
    */
  }
  bool Collect(absl::FunctionRef<void(T&)> f) {
    if (rcu_.TryRead()) {
      if (Value& owned = rcu_.Read(); owned.version == collect_version_) {
        collect_version_ = ++owned.version;
        LOG(INFO) << "Collected value " << owned.primary << "@"
                  << int{owned.version};
        f(owned.primary);
        return true;
      }
    }
    LOG(INFO) << "No value to collect";
    return false;
    /*
    f(owned.primary);
    owned.version++;
    // Prepare a copy for the updater, and also destroy any previous value.
    owned.collect_copy = owned.primary;
    */
  }

  std::optional<T> Collect() {
    return Collect<T>([](T& ref) { return std::exchange(ref, T{}); });
  }

 private:
  struct Value {
    int_fast8_t version = 0;
    T primary;
    T collect_copy;
  };

  template <typename U>
  static void OptionalSwap(std::optional<U>& left, U& right) {
    if (left.has_value()) {
      std::swap(*left, right);
    } else {
      left = std::move(right);
    }
  }

  Local3StateRcu<Value> rcu_;
  int_fast8_t update_version_ = 0;
  int_fast8_t collect_version_ = 0;
  T copy_from_collect_;
};

}  // namespace simple_rcu
