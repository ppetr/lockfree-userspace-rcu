#include <type_traits>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/utility/exchange.h"
#include "local_3state_rcu.h"

namespace simple_rcu {

// Generic metric set that allows fast, atomic, lock-free writes to metrics.
//
// `T` must be copyable. Commonly it's `std::shared_ptr<const U>`.
template <typename T>
class MetricSet {
  static_assert(std::is_default_constructible<T>::value,
                "T must be default constructible");
  static_assert(std::is_copy_constructible<T>::value &&
                    std::is_copy_assignable<T>::value,
                "T must be copy constructible and assignable");
  // TODO: Add a static_assert for `operator+=`.

 public:
  class Local;

  // Holds a read reference to a RCU value for the current thread.
  // The reference is guaranteed to be stable during the lifetime of `WriteRef`.
  // Callers are expected to limit the lifetime of `WriteRef` to as short as
  // possible.
  // Thread-compatible (but not thread-safe), reentrant.
  class WriteRef final {
   public:
    WriteRef(WriteRef&& other) : WriteRef(other.registrar_) {}
    WriteRef(const WriteRef& other) : WriteRef(other.registrar_) {}
    WriteRef& operator=(WriteRef&&) = delete;
    WriteRef& operator=(const WriteRef&) = delete;

    ~WriteRef() {
      if (--registrar_.read_depth_ == 0) {
        registrar_.local_rcu_.TriggerRead();
      }
    }

    const T* operator->() const { return &**this; }
    T* operator->() { return &**this; }
    const T& operator*() const { return *registrar_.local_rcu_.Read(); }
    T& operator*() { return registrar_.local_rcu_.Read(); }

   private:
    WriteRef(Local& registrar) : registrar_(registrar) {
      registrar_.read_depth_++;
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
    Local(MetricSet& rcu) LOCKS_EXCLUDED(rcu.lock_)
        : rcu_(rcu), read_depth_(0), local_rcu_() {
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
    WriteRef Read() { return WriteRef(*this); }

   private:
    // Thread-compatible.
    T Collect() EXCLUSIVE_LOCKS_REQUIRED(rcu_.lock_) {
      local_rcu_.TriggerUpdate();
      return absl::exchange(local_rcu_.Update(), T());
    }

    MetricSet& rcu_;
    // Incremented with each `WriteRef` instance. Ensures that `TriggerRead` is
    // invoked only for the outermost `WriteRef`, keeping its value unchanged
    // for its whole lifetime.
    int_fast16_t read_depth_;
    Local3StateRcu<T> local_rcu_;

    friend class MetricSet;
  };

  // Constructs a RCU with an initial value `T()`.
  MetricSet() : lock_(), value_(), threads_() {}

  // Updates `value` in all registered `Local` threads.
  // Returns the previous value. Note that the previous value can still be
  // observed by readers that haven't obtained a fresh `WriteRef()` instance.
  //
  // This method isn't tied in any particular way to a `Local` instance
  // corresponding to the current thread, and can be called also by threads
  // that have no `Local` instance at all.
  //
  // Thread-safe.
  T Collect() LOCKS_EXCLUDED(lock_) {
    absl::MutexLock mutex(&lock_);
    std::swap(value_, value);
    for (Local* thread : threads_) {
      value_ += thread->Collect();
    }
    return value_;
  }

 protected:
  // TODO.
  // Needs to be just thread-compatible.
  virtual void Collect(T collected_value) = 0;

 private:
  absl::Mutex lock_;
  // List of registered thread-`Local` instances.
  absl::flat_hash_map<Local*, absl::flat_hash_map<Metric*>> threads_ GUARDED_BY(lock_);
};

}  // namespace simple_rcu
