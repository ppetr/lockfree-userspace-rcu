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

// Provides a RCU framework between two threads (hence "Local"):
//
// - Reader, which consumes values provided by the Updater, and which is
//   expected to be very fast.
// - Updater, which provides new values to be made available to the Reader,
//   and which is expected to do the "heavy", slow updates of values.
//
// Contract:
// - A value written to `Update()` and submitted by `TriggerUpdate()` is then
//   seen by the reader thread in `Read()` after it calls `TriggerRead()`.
// - While there are no new calls to `TriggerUpdate()`, calls to
//   `TriggerRead()` are idemponent - `Read()` continues to return the same
//   value.
// - A value written to (or just abandoned in) `Read()` and submitted by
//   `TriggerRead()' that returns `true`, is then seen by the updater thread in
//   `Update()` after it calls `TriggerUpdate()`. The updater thread then can
//   do any cleanups needed and overwrite the value to be submitted by the next
//   call of `TriggerUpdate()`.
//   Unlike the previous case, calls to `TriggerUpdate()` aren't idempotent -
//   there is an asymetry between the reader and the updater, even though both
//   can modify values: Calls to `TriggerUpdate` always submit a new value,
//   even if the previous one hasn't been seen by the reader thread yet.
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
  Local3StateRcu()
      : values_{T(), T(), T()},
        next_read_index_(1),
        read_{.index = 0},
        update_{.index = 2, .next_index = 1} {}
  ~Local3StateRcu() noexcept = default;

  // Reference to the value that can be manipulated by the reading thread.
  T& Read() noexcept { return values_[read_.index]; }

  // Signal that the reader thread is ready to consume a new value.
  //
  // Returns `true` if `Read()` now points to such a new value, invalidating
  // any previous reference obtained from `Read()`.
  // Returns `false` otherwise, that is, there is no new value available and
  // the reference pointed to by `Read()` remains unchanged.
  bool TriggerRead() noexcept {
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

  // Makes the value stored in `Update()` available to the reader and changes
  // `Update()` to point to a value to be updated next.
  //
  // Returns `true` iff `Update()` now points to a value used by the reader
  // that should be reclaimed. Otherwise `Update()` points to a previous update
  // value that hasn't been (and won't ever be) seen by the reader.
  //
  // In both cases any previous reference obtained by `Update()` is
  // invalidated.
  bool TriggerUpdate() noexcept {
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

  // Similar to `TriggerUpdate`, but triggers an update only if the reader has
  // advanced and is waiting for a new value.
  // In other words, never overwrites an update value that hasn't been seen by
  // the reader.
  // Returns `true` if an update was triggered and now `Update()` holds the
  // reclaimed value from the reader. Otherwise returns `false`, which signals
  // no action, in particular `Update()` points to the same value as before.
  bool TryTriggerUpdate() noexcept {
    Index old_next_read_index = kNullIndex;
    if (next_read_index_.compare_exchange_strong(
            old_next_read_index, update_.index, std::memory_order_acq_rel)) {
      update_.RotateAfterNext();
      return true;
    } else {
      // The reader hasn't advanced yet. Nothing to do.
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
