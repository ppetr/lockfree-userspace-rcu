#include <type_traits>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "simple_rcu/local_3state_rcu.h"

namespace simple_rcu {

// Generic, user-space RCU implementation with fast, atomic, lock-free reads.
//
// `T` must be copyable. Commonly it's `std::shared_ptr<const U>`.
template <typename T>
class Rcu {
 public:
  class Local;
  using MutableT = typename std::remove_const<T>::type;

  static_assert(std::is_default_constructible<MutableT>::value,
                "T must be default constructible");
  static_assert(std::is_copy_constructible<MutableT>::value &&
                    std::is_copy_assignable<MutableT>::value,
                "T must be copy constructible and assignable");

  // Holds a read reference to a RCU value for the current thread.
  // The reference is guaranteed to be stable during the lifetime of `Snapshot`.
  // Callers are expected to limit the lifetime of `Snapshot` to as short as
  // possible.
  // Thread-compatible (but not thread-safe), reentrant.
  class Snapshot final {
   public:
    Snapshot(Snapshot&& other) noexcept : Snapshot(other.registrar_) {}
    Snapshot(const Snapshot& other) noexcept : Snapshot(other.registrar_) {}
    Snapshot& operator=(Snapshot&&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    ~Snapshot() noexcept { registrar_.snapsnot_depth_--; }

    const T* operator->() const noexcept { return &**this; }
    T* operator->() noexcept { return &**this; }
    const T& operator*() const noexcept {
      return *registrar_.local_rcu_.Read();
    }
    T& operator*() noexcept { return registrar_.local_rcu_.Read(); }

   private:
    Snapshot(Local& registrar) noexcept : registrar_(registrar) {
      if (registrar_.snapsnot_depth_++ == 0) {
        registrar_.local_rcu_.TriggerRead();
      }
    }

    Local& registrar_;

    friend class Local;
  };

  // Interface to the RCU local to a particular reader thread.
  // Construction and destruction are thread-safe operations, but the `Read()`
  // method is only thread-compatible. Callers are expected to construct a
  // separate `Local` instance for each reader thread.
  class Local final {
   public:
    // Thread-safe.
    Local(Rcu& rcu) LOCKS_EXCLUDED(rcu.lock_)
        : rcu_(rcu), snapsnot_depth_(0), local_rcu_() {
      absl::MutexLock mutex(&rcu_.lock_);
      rcu_.threads_.insert(this);
      Update(rcu_.value_);
    }
    ~Local() LOCKS_EXCLUDED(rcu_.lock_) {
      absl::MutexLock mutex(&rcu_.lock_);
      rcu_.threads_.erase(this);
    }

    // Obtains a read snapshot to the current value held by the RCU.
    // This is a very fast, lock-free and atomic operation.
    // Thread-compatible, but not thread-safe.
    Snapshot Read() noexcept { return Snapshot(*this); }

   private:
    // Thread-compatible.
    void Update(MutableT value) EXCLUSIVE_LOCKS_REQUIRED(rcu_.lock_) {
      std::swap(local_rcu_.Update(), value);
      local_rcu_.TriggerUpdate();
      // As a small performance optimization, destroy old `value` only after
      // triggering update with the new value.
    }

    Rcu& rcu_;
    // Incremented with each `Snapshot` instance. Ensures that `TriggerRead` is
    // invoked only for the outermost `Snapshot`, keeping its value unchanged
    // for its whole lifetime.
    int_fast16_t snapsnot_depth_;
    Local3StateRcu<MutableT> local_rcu_;

    friend class Rcu;
  };

  // Constructs a RCU with an initial value `T()`.
  Rcu() : Rcu(T()) {}
  Rcu(T initial_value)
      : lock_(), value_(std::move(initial_value)), threads_() {}

  // Updates `value` in all registered `Local` threads.
  // Returns the previous value. Note that the previous value can still be
  // observed by readers that haven't obtained a fresh `Snapshot` instance yet.
  //
  // This method isn't tied in any particular way to a `Local` instance
  // corresponding to the current thread, and can be called also by threads
  // that have no `Local` instance at all.
  //
  // Thread-safe.
  T Update(typename std::remove_const<T>::type value) LOCKS_EXCLUDED(lock_) {
    absl::MutexLock mutex(&lock_);
    std::swap(value_, value);
    for (Local* thread : threads_) {
      thread->Update(value_);
    }
    return value;
  }

 private:
  absl::Mutex lock_;
  // The current value that has been distributed to all thread-`Local`
  // instances.
  MutableT value_ GUARDED_BY(lock_);
  // List of registered thread-`Local` instances.
  absl::flat_hash_set<Local*> threads_ GUARDED_BY(lock_);
};

}  // namespace simple_rcu
