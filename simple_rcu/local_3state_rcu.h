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

namespace simple_rcu {

// Provides a RCU-like framework to exchange values between just two threads
// (hence "Local"):
//
// - Reader, which accesses an instance of `T` at `Read()`.
// - Updater, which accesses an instance of `T` at `Update()`.
//
// Contract:
// - A value written to `Update()` by the Updater and submitted by
//   `ForceUpdate()` is then seen by the Reader in `Read()` after it calls
//   `TryRead()`.
// - While there the Updater doesn't call `ForceUpdate()` or `TryUpdate()`,
//   calls to `TryRead()` are idemponent - `Read()` continues to return the
//   same reference.
// - A value written to (or just abandoned in) `Read()` and submitted by
//   `TryRead()' that returns `true`, is then seen by the updater thread in
//   `Update()` after it calls `TryUpdate()` or `ForceUpdate()`. Similarly to
//   above `TryUpdate()` is idempotent while the other thread doesn't call
//   `TryRead()`.
// - On the other hand, calls to `ForceUpdate()` aren't idempotent - each call
//   always submits a new value, possibly overwriting the previous one, if it
//   hasn't been accepted by the Reader yet.
//
// Internally the class manages 3 instances of `T`: One for `Read()`, one for
// `Update()` and one that is "in flight" in between the two threads.
//
// The implementation uses only atomic operations and does no memory
// allocations on its own - it only "juggles" the 3 instances of `T`
// internally between the two threads.
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

  // Signal that the reader thread is ready to advance to a new value.
  //
  // Returns `true` if `Read()` now points to such a new value, invalidating
  // any previous reference obtained from `Read()`.
  // Returns `false` otherwise, that is, there is no new value available and
  // the reference pointed to by `Read()` remains unchanged.
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

  // Returns `true` if `Update()` now points to such a new value, invalidating
  // any previous reference obtained from `Update()`.
  // Returns `false` otherwise, that is, there is no new value available and
  // the reference pointed to by `Update()` remains unchanged.
  //
  // See also `TryRead()` which has the same semantics for the reader
  // thread.
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

  // Makes the value stored in `Update()` available to the reader and changes
  // `Update()` to point to a value to be updated next.
  //
  // Returns `true` iff `Update()` now points to a value used by the reader
  // that should be reclaimed. Otherwise `Update()` points to a previous update
  // value that hasn't been (and won't ever be) seen by the reader.
  //
  // In both cases any previous reference obtained by `Update()` is
  // invalidated.
  //
  // Compared to `TryUpdate()` this method forces an update even if the
  // reader hasn't advanced yet. In such a case a previous value passed by the
  // updater (and waiting for the reader) is overwritten.
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

 private:
  using Index = int_fast8_t;
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
      Index old_read_index = (0 + 1 + 2) - (index + next_index);
      next_index = index;
      index = old_read_index;  // To be reclaimed.
    }

    // The updater thread can manipulate the value at this index.
    Index index;
    // The last known value of `next_read_index_` known to the updater thread.
    Index next_index;
  } update_;
};

}  // namespace simple_rcu

#endif  // _SIMPLE_RCU_LOCAL_3STATE_RCU_H
