// Copyright 2020 Google LLC
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

#ifndef _REFCOUNT_STRUCT_H
#define _REFCOUNT_STRUCT_H

// Type-safe, reference-counted pointers to variable-sized classes.
//
// - Ref<T> is move-only, owns a memory location with an instance of T.
//   Always contains a value (unless moved out).
// - Ref<const T> is copy-only, allows only `const` access to an instance of T.
//   Always contains a value (unless moved out).
// - Ref is extremely lightweight, contain only a single pointer.
// - When creating an instance of `T`, an additional block of memory can be
//   requested that is passed to the constructor of `T`.
// - Creating an instance of `T` does a single memory allocation (unlike
//   `shared_ptr`), even with an additional memory block.
// - Shared::ToDeleterArg allows to create a `void*` reference that can be used
//   to release memory in old-style C code using Shared::Deleter.

#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <utility>

#ifdef __has_include
#if __has_include(<optional>)
#include <optional>
#endif  // __has_include(<optional>)
#endif  // __has_include

namespace refptr {

class Refcount {
 public:
  constexpr Refcount() : count_{1} {}

  // Increments the reference count. Imposes no memory ordering.
  inline void Inc() {
    // No need for any synchronization/ordering, as the value is not inspected
    // at all.
    count_.fetch_add(1, std::memory_order_relaxed);
  }

  // Returns whether the atomic integer is 1.
  inline bool IsOne() {
    // This thread must observe the correct value, including any prior
    // modifications by other threads.
    return count_.load(std::memory_order_acquire) == 1;
  }

  // Returns `true` iff the counter's value is zero after the decrement
  // operation. In such a case the caller must destroy the referenced object,
  // and the counter's state becomes undefined.
  //
  // A caller should pass `expect_one = true` if there is a reasonable chance
  // that there is only a single reference to the object. This allows slight
  // performane optimization when reqesting the appropriate memory barriers.
  inline bool Dec(bool expect_one = false) {
    // This thread must observe the correct value if `refcount` reaches zero,
    // including any prior modifications by other threads. All other threads
    // must observe the result of the operation.
    if (expect_one && IsOne()) {
      // Knowing the object will be destructed, we don't decrement the counter.
      // This way, we save the _release operation_ that would be needed for
      // decrementing it below.
      return true;
    }
    int32_t refcount = count_.fetch_sub(1, std::memory_order_acq_rel);
    assert(refcount >= 0);
    return refcount == 0;
  }

 private:
  std::atomic<int32_t> count_;
};

// Owns a block of memory large enough to store a properly aligned instance of
// `T` and additional `size` number of elements of type `A`.
template <typename T, typename A = char>
class Placement {
 public:
  // Allocates memory for a properly aligned instance of `T`, plus additional
  // array of `size` elements of `A`.
  explicit Placement(size_t size)
      : size_(size),
        allocation_(reinterpret_cast<char*>(
            std::allocator<Placeholder>().allocate(AllocatedBytes()))) {
    static_assert(std::is_trivial<Placeholder>::value);
  }
  Placement(Placement const&) = delete;
  Placement(Placement&& other) {
    allocation_ = other.allocation_;
    size_ = other.size_;
    other.allocation_ = nullptr;
  }

  ~Placement() {
    if (allocation_) {
      std::allocator<Placeholder>().deallocate(AsPlaceholder(),
                                               AllocatedBytes());
    }
  }

  // Returns a pointer to an uninitialized memory area available for an
  // instance of `T`.
  T* Node() const { return reinterpret_cast<T*>(&AsPlaceholder()->node); }
  // Returns a pointer to an uninitialized memory area available for
  // holding `size` (specified in the constructor) elements of `A`.
  A* Array() const { return reinterpret_cast<A*>(&AsPlaceholder()->array); }
  // Returns a pointer to an uninitialized memory area available for an
  // instance of `T`, and a pointer to an area suitable for holding `size`
  // (specified in the constructor) elements of `A`.

  size_t Size() { return size_; }

 private:
  // Holds a properly aligned instance of `T` and an array of length 1 of `A`.
  struct Placeholder {
    typename std::aligned_storage<sizeof(T), alignof(T)>::type node;
    // The array type must be the last one in the struct.
    typename std::aligned_storage<sizeof(A[1]), alignof(A[1])>::type array;
  };

  Placeholder* AsPlaceholder() const {
    return reinterpret_cast<Placeholder*>(allocation_);
  }

  size_t AllocatedBytes() {
    return sizeof(Placeholder) + (size_ - 1) * sizeof(A);
  }

  size_t size_;
  char* allocation_;
};

template <typename T>
class Ref;

// Holds a instance of `T` in a self-owned block of memory. The instance is
// properly destroyed and the block of memory freed once the reference count
// drops to zero.
// The memory block can be optionally larger to provide additional piece of
// memory within a single allocation block.
//
// See also: https://isocpp.org/wiki/faq/dtors#memory-pools
template <typename T, typename A = char,
          typename std::enable_if<!std::is_destructible<A>{} ||
                                      std::is_trivially_destructible<A>{},
                                  bool>::type = true>
class Refcounted {
 public:
  // Increment the internal reference counter. Must be paired with `Dec`.
  inline void Inc() const { refcount_.Inc(); }

  // The type is marked as `&&` to emphasize (and enforce by some compilers)
  // that the caller must not make any guarantees about the object once this is
  // called - any call to Dec can be the last one that destroys the object.
  void Dec(bool expect_one = false) const&& {
    if (refcount_.Dec(expect_one)) {
      // Ensure deallocation even in the (rare) case of an exception.
      Placement<Refcounted<T>> deallocator(std::move(placement_));
      this->~Refcounted<T>();
    }
  }

  // Returns `true` iff the refcount is 1, that is, the caller is the sole
  // owner of the object.
  inline bool IsOne() const { return refcount_.IsOne(); }

  // Allocates and constructs in place an instance of `T`.
  template <typename... Arg>
  static Refcounted<T>* Allocate(Arg&&... args) {
    Placement<Refcounted<T, A>, A> placement(0);
    Refcounted<T, A>* aligned = placement.Node();
    return new (aligned)
        Refcounted<T, A>(std::move(placement), std::forward<Arg>(args)...);
  }

  // Allocates and constructs in place an instance of `T`, with an additional
  // buffer of `length` bytes.
  template <typename... Arg>
  static Refcounted<T, A>* AllocateWithBlock(size_t length, Arg&&... args) {
    Placement<Refcounted<T, A>> placement(length);
    auto* node = placement.Node();
    auto* array = new (placement.Array()) char[length];
    return new (node) Refcounted<T, A>(std::move(placement), array, length,
                                       std::forward<Arg>(args)...);
  }

  friend class Ref<T>;
  friend class Ref<typename std::add_const<T>::type>;

 private:
  template <typename... Arg>
  Refcounted(Placement<Refcounted<T, A>, A> placement, char* buffer,
             size_t length, Arg&&... args)
      : placement_(std::move(placement)),
        refcount_(),
        nested_(buffer, length, std::forward<Arg>(args)...) {}

  template <typename... Arg>
  explicit Refcounted(Placement<Refcounted<T, A>, A> placement, Arg&&... args)
      : placement_(std::move(placement)),
        refcount_(),
        nested_(std::forward<Arg>(args)...) {}

  mutable Placement<Refcounted<T, A>, A> placement_;
  mutable Refcount refcount_;
  T nested_;
};

// References a ref-counted instance of `T`.
//
// Instances of `Ref<const T>` are copyable, because there is no risk of
// concurrent modifications.
// Instances of `Ref<T>`, where `T` is non-`const`, are move-only.
template <typename T>
class Ref final {
 public:
  Ref(Ref<typename std::add_const<T>::type> const& other) {
    Reset(other.buffer_);
  }
  Ref(Ref<typename std::remove_const<T>::type>&& other)
      : buffer_(other.buffer_) {
    other.buffer_ = nullptr;
  }

  Ref& operator=(Ref<typename std::add_const<T>::type> const& other) {
    assert(other.buffer_ != nullptr);
    Reset(other.buffer_);
  }
  Ref& operator=(Ref<typename std::remove_const<T>::type>&& other) {
    Reset(nullptr);
    buffer_ = other.buffer_;
    other.buffer_ = nullptr;
    return *this;
  }

  ~Ref() { Reset(nullptr); }

  template <typename U, typename... Arg>
  friend Ref<U> New(size_t length, Arg&&... args);

#ifdef __cpp_lib_optional
  // If `this` is the only instance referencing the internal data-structure,
  // converts it into a non-const Ref. Otherwise it is discarded.
  //
  // Example:
  //
  //   std::optional<Ref<T>> reused = std::nullopt;
  //   while (HaveDate()) {
  //     Ref<T> buffer(reused
  //        ? *std::move(reused)
  //        : Ref<T>::AllocateWithBlock(...));
  //     // Fill buffer with data.
  //     Ref<const T> shared = std::move(reused);
  //     consumer.AppendData(shared);
  //     // If the consumer released the shared pointer, the buffer can be
  //     // reused.
  //     reused = std::move(shared).AttemptToClaim();
  //   }
  //
  // Here if `consumer` keeps `shared` alive (for example building them into a
  // data structure), a new buffer is allocated. If it releases `shared` by the
  // time `AppendData` finishes, the buffer is reused, so no new memory
  // allocation is needed.
  std::optional<Ref<typename std::remove_const<T>::type>> AttemptToClaim() && {
    if (buffer_->IsOne()) {
      std::optional<Ref<typename std::remove_const<T>::type>> result(
          (Ref<typename std::remove_const<T>::type>)(buffer_));
      // Don't call Reset here, the ownership is passed to the returned value.
      buffer_ = nullptr;
      return result;
    } else {
      Reset(nullptr);
      return std::nullopt;
    }
  }
#endif  // __cpp_lib_optional

  T& operator*() const { return buffer_->nested_; }

  const T* operator->() const { return &buffer_->nested_; }

  // Creates a new reference to this object and returns it expressed as a raw
  // pointer. It must be passed to the Deleter function exactly once to
  // release it.
  void* ToDeleterArg() const {
    assert(buffer_ != nullptr);
    buffer_->Inc();
    return static_cast<void*>(buffer_);
  }

  // Releases a reference created by `ToDeleterArg`.
  // If such a pointer is passed more than once, the behavior is undefined.
  // If a pointer different from one created by `ToDeleterArg` is passed, the
  // behavior is undefined.
  static void Deleter(void* shared_buffer_ptr) {
    if (shared_buffer_ptr) {
      std::move(*static_cast<Refcounted<T>*>(shared_buffer_ptr)).Dec();
    }
  }

  friend class Ref<typename std::add_const<T>::type>;
  friend class Ref<typename std::remove_const<T>::type>;

 private:
  // Creates a reference with a reference counter of one.
  explicit Ref(Refcounted<typename std::remove_const<T>::type>* buffer)
      : buffer_(buffer) {
    assert(buffer != nullptr && buffer_->IsOne());
  }

  // Replace the internal reference by another one, properly
  // decrementing and incrementing their counters.
  inline void Reset(Refcounted<typename std::remove_const<T>::type>* buffer) {
    if (buffer_ != nullptr) {
      // Non-const values should not be shared, therefore we can expect
      // only a single owner.
      std::move(*buffer_).Dec(/*expect_one=*/!std::is_const<T>::value);
    }
    if ((buffer_ = buffer) != nullptr) {
      buffer_->Inc();
    }
  }

  Refcounted<typename std::remove_const<T>::type>* buffer_;
};

// Constructs a new instance of `U` in-place, with the given arguments with an
// additional block of memory of size `length`. A `char*` pointer to this
// buffer and its `size_t` length are passed as the first two arguments to a
// constructor of `U`.
template <typename U, typename... Arg>
inline Ref<U> New(size_t length, Arg&&... args) {
  return Ref<U>(
      Refcounted<typename std::remove_const<U>::type>::AllocateWithBlock(
          length, std::forward<Arg>(args)...));
}

}  // namespace refptr

#endif  // _REFCOUNT_STRUCT_H
