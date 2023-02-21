// Copyright 2022 Google LLC
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

#ifndef _SIMPLE_RCU_LOCAL_3STATE_RCU_H
#define _SIMPLE_RCU_LOCAL_3STATE_RCU_H

#include <array>
#include <atomic>
#include <cstddef>

namespace simple_rcu {

// Provides a RCU-like framework to exchange values between just two threads
// "Reader" and "Updater" (hence "Local"). It consists of 3 instances of `T`
// such that:
//
// - One is accessed by the Reader via the `Read()` reference.
// - One is accessed by the Updater via the `Update()` reference.
// - The last one is "in flight", being passed either:
//   * From the Updater to the Reader ("U->R", the initial state).
//   * From the Reader to the Updater ("R->U").
//
// No two `...Read...` methods may be called concurrently. Similarly, no two
// `...Update...` methods. This is usually accomplished by having one thread
// access only the `...Read...` methods (the Reader) and another (the Updater)
// only the `...Update...` methods.
//
// The Reader has a more passive role, only advancing to a new value when one
// is provided by the Updater. The Updater allows more varied access to both
// `Update()` and the in-flight values.
//
// See the documentation of the operations below for the description how they
// interact with the three states.
//
// The implementation uses only atomic operations and does no memory
// allocations during its operation - it only "juggles" the 3 pre-allocated
// instances of `T` internally between the two threads. If they need to be
// constructed and deconstructed as they pass between the Updater and the
// Reader, wrap `T` into `absl::optional` or `std::unique_ptr`.
template <typename T>
class Local3StateRcu {
 public:
  // Builds an instance by initializing the internal three `T` variables to
  // given values in the initial state:
  //
  // - `read` is the value that'll be available in `Read()`. `TryRead()`
  //    will return `false`.
  // - `update` is the value that'll be available in `Update()`.
  //   `TryUpdate()` will return `true` and the value reclaimed afterwards
  //   in `Update` will be `reclaimed`.
  //
  // `T` must be moveable.
  Local3StateRcu(T read, T update, T reclaim)
      : values_{std::move(read), std::move(update), std::move(reclaim)},
        next_read_index_(kNullIndex),
        read_{.index = 0},
        update_{.index = 1, .next_index = 0} {}
  // Builds an instance by initializing the internal three `T` variables to a
  // given single values. `T` must be copyable.
  explicit Local3StateRcu(const T& value)
      : Local3StateRcu(value, value, value) {}
  // Builds an instance by initializing the internal three `T` variables to
  // `T()`. `T` must be default-constructible.
  Local3StateRcu()
      : values_(),
        next_read_index_(kNullIndex),
        read_{.index = 0},
        update_{.index = 1, .next_index = 0} {}
  ~Local3StateRcu() noexcept = default;

  // Reference to the value that can be manipulated by the reading thread.
  T& Read() noexcept { return values_[read_.index]; }
  const T& Read() const noexcept { return values_[read_.index]; }

  // Advance the Reader to a new value, if possible.
  //
  // If the in-flight instance is "U->R", it becomes bound to `Read()`, the
  // value previosly pointed to by `Read() becomes in flight "R->U", and `true`
  // is returned. In this case the previous reference returned by `Read()` must
  // be considered invalid and must not be used any more.
  //
  // If the in-flight instance is already "R->U", does nothing and returns
  // `false`.
  //
  // See also `TryUpdate()` which has the same semantics for the updater
  // thread.
  bool TryRead() noexcept {
    Index next_read_index =
        next_read_index_.exchange(kNullIndex, std::memory_order_acq_rel);
    if (next_read_index != kNullIndex) {
      read_.index = next_read_index;
      return true;
    } else {
      return false;
    }
  }

  // Reference to the value that can be manipulated by the updating thread.
  T& Update() noexcept { return values_[update_.index]; }
  const T& Update() const noexcept { return values_[update_.index]; }

  // Advance the Updater to a new value, if possible.
  //
  // If the in-flight instance is "R->U", it becomes bound to `Update()`, the
  // value previosly pointed to by `Update() becomes in flight "U->R", and
  // `true` is returned. In this case the previous reference returned by
  // `Update()` must be considered invalid and must not be used any more.
  //
  // If the in-flight instance is already "U->R", does nothing and returns
  // `false`.
  //
  // See also `TryRead()` which has the same semantics for the updater thread.
  bool TryUpdate() noexcept {
    Index old_next_read_index = kNullIndex;
    // Use relaxed memory ordering on failure, since in this case there is no
    // related observable memory access.
    if (next_read_index_.compare_exchange_strong(
            old_next_read_index, update_.index,
            /*success=*/std::memory_order_acq_rel,
            /*failure=*/std::memory_order_relaxed)) {
      update_.RotateAfterNext();
      return true;
    } else {
      // The reader hasn't advanced yet. Nothing to do.
      return false;
    }
  }

  // Makes the value stored in `Update()` the new in-flight "U->R" value.
  // Therefore it becomes available to the Reader regardless of the previous
  // state. The previous in-flight instance becomes bound to `Update()`,
  // whether it was "R->U" or "U->R".
  //
  // Returns `true` if the previous state of the in-flight instances was
  // "R->U", `false` if it was "U->R".
  //
  // In both cases the previous reference returned by `Update()` must be
  // considered invalid and must not be used any more.
  //
  // Compared to `TryUpdate()` this method forces an update even if the
  // reader hasn't advanced yet.
  bool ForceUpdate() noexcept {
    Index old_next_read_index =
        next_read_index_.exchange(update_.index, std::memory_order_acq_rel);
    if (old_next_read_index == kNullIndex) {
      update_.RotateAfterNext();
      return true;
    } else {
      // The reader hasn't advanced yet.
      // This is just a swap of update_index_ and next_read_index_.
      update_.next_index = update_.index;
      update_.index = old_next_read_index;
      return false;
    }
  }

  // Returns a pointer to the in-flight instance if it is "R->U". The returned
  // pointer is valid only until one of the state-changing Updater's methods is
  // called. If the in-flight instance is "U->R", returns `nullptr`.
  //
  // This allows to access the instance passed by the Reader to the Updater
  // without providing a new value by `ForceUpdate()` or `TryUpdate()`.
  T* ReclaimByUpdate() noexcept {
    if (next_read_index_.load(std::memory_order_acquire) == kNullIndex) {
      return &values_[update_.OldReadIndex()];
    } else {
      return nullptr;
    }
  }

 private:
#ifdef __cpp_lib_atomic_lock_free_type_aliases
  using Index = typename std::atomic_signed_lock_free::value_type;
#else
  using Index = std::ptrdiff_t;
#endif
#ifdef __cpp_lib_atomic_is_always_lock_free
  static_assert(std::atomic<Index>::is_always_lock_free,
                "Not lock-free on this architecture, please report this as a "
                "bug on the project's GitHub page");
#endif

  static constexpr Index kNullIndex = -1;

  // Storage for instances of `T` that are juggled around between the reader
  // and updater threads.
  // All the variables below are indices into `values_`, that is, from set
  // {0, 1, 2}.
  std::array<T, 3> values_;
  // If `kNullIndex`, there is no new value available to the reader thread.
  // Invariants in this case:
  //  read_.index == update_.next_index != update_.index
  // Otherwise it contains the index holding a new value available to the
  // reader.
  // Invariants in this case:
  //  * {read_.index, update_.index, update_.next_index} = {0, 1, 2}
  //  * next_read_index_.load() == update_.next_index
  std::atomic<Index> next_read_index_;
  // Accessed only by the "read" thread:
  struct {
    // The reader thread can manipulate the value at this index.
    Index index;
  } read_;
  // Accessed only by the "update" thread.
  struct {
    // After `index` is pushed to `next_read_index_` above, rotate remaining
    // indices: next_index <- index <- old read index.
    inline void RotateAfterNext() noexcept {
      Index old_read_index = OldReadIndex();
      next_index = index;
      index = old_read_index;  // To be reclaimed.
    }

    Index OldReadIndex() noexcept {
      return (0 + 1 + 2) - (index + next_index);
      ;
    }

    // The updater thread can manipulate the value at this index.
    Index index;
    // The last known value of `next_read_index_` known to the updater thread.
    Index next_index;
  } update_;
};

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_LOCAL_3STATE_RCU_H
