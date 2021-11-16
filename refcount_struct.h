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

// Type-safe, reference-counted pointers classes.
//
// - Ref<T> is move-only, owns a memory location with an instance of T.
//   Always contains a value (unless moved out).
// - Ref<const T> is copy-only, allows only `const` access to an instance of T.
//   Always contains a value (unless moved out).
// - Ref is extremely lightweight, contain only a single pointer.
// - When creating an instance of `T`, additional memory can be requested that
//   is passed to the constructor of `T`.
// - Creating an instance of `T` does a single memory allocation (unlike
//   `shared_ptr`), even with additional memory.
// - Shared::ToDeleterArg allows to create a `void*` reference that can be used
//   to release memory in old-style C code using Shared::Deleter.

#include <atomic>
#include <cassert>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#ifdef __has_include
#if __has_include(<variant>)
#include <variant>
#endif  // __has_include(<variant>)
#endif  // __has_include

#include "refcount.h"

namespace refptr {

template <typename T, class Deleter>
class Ref;

namespace internal {

// Distinguishes `Ref<T>` (`unique`) and `Ref<const T>` (`shared`).
enum class OwnershipTraits { shared = 0, unique = 1 };

template <typename T>
struct BaseOwnershipTraits {
  static constexpr OwnershipTraits traits = std::is_const<T>::value
                                                ? OwnershipTraits::shared
                                                : OwnershipTraits::unique;
};

template <typename T, class Deleter, OwnershipTraits>
class RefBase;

template <typename T, class Deleter>
class RefBase<T, Deleter, OwnershipTraits::shared> {
 public:
  RefBase(RefBase const &other) { Reset(other.buffer_); }
  RefBase(RefBase &&other) : buffer_(other.buffer_) { other.buffer_ = nullptr; }

  RefBase &operator=(RefBase const &other) {
    assert(other.buffer_ != nullptr);
    Reset(other.buffer_);
    return *this;
  }
  RefBase &operator=(RefBase &&other) {
    assert(other.buffer_ != nullptr);
    Reset(nullptr);
    buffer_ = other.buffer_;
    other.buffer_ = nullptr;
    return *this;
  }

  ~RefBase() { Reset(nullptr); }

  const T &operator*() const { return buffer_->nested; }
  const T *operator->() const { return &buffer_->nested; }

#ifdef __has_include
#if __has_include(<variant>)
  std::variant<Ref<T, Deleter>, Ref<const T, Deleter>> AttemptToClaim() &&;
#endif  // __has_include(<variant>)
#endif  // __has_include

 protected:
  constexpr explicit RefBase(const Refcounted<T, Deleter> *buffer)
      : buffer_(buffer) {}

  inline void Reset(const Refcounted<T, Deleter> *buffer) {
    if ((buffer_ != nullptr) && buffer_->refcount.Dec()) {
      const_cast<Refcounted<T, Deleter> &&>(std::move(*buffer_)).SelfDelete();
    }
    if ((buffer_ = buffer) != nullptr) {
      buffer_->refcount.Inc();
    }
  }

  Refcounted<T, Deleter> *move_buffer() && {
    auto *buffer = buffer_;
    buffer_ = nullptr;
    return const_cast<Refcounted<T, Deleter> *>(buffer);
  }

  const Refcounted<T, Deleter> *buffer_;
};

template <typename T, class Deleter>
class RefBase<T, Deleter, OwnershipTraits::unique> {
 public:
  RefBase(RefBase const &other) = delete;
  RefBase(RefBase &&other) : buffer_(other.buffer_) { other.buffer_ = nullptr; }

  RefBase &operator=(RefBase const &other) {
    Reset(other.buffer_);
    return *this;
  }
  RefBase &operator=(RefBase &&other) = delete;

  ~RefBase() { Reset(nullptr); }

  T &operator*() { return buffer_->nested; }
  T *operator->() { return &buffer_->nested; }

  Ref<const T, Deleter> Share() &&;

 protected:
  constexpr explicit RefBase(Refcounted<T, Deleter> *buffer)
      : buffer_(buffer) {}

  inline void Reset(Refcounted<T, Deleter> *buffer) {
    if (buffer_ != nullptr) {
      assert(buffer_->refcount.IsOne());
      std::move(*buffer_).SelfDelete();
    }
    buffer_ = buffer;
  }

  Refcounted<T, Deleter> *move_buffer() && {
    auto *buffer = buffer_;
    buffer_ = nullptr;
    return buffer;
  }

  Refcounted<T, Deleter> *buffer_;
};

}  // namespace internal

// Holds a reference counted instance of `T` in a self-owned block of memory.
// The instance is properly destroyed and the block of memory freed once the
// reference count drops to zero.
//
// Instances of `Ref<const T>` are copyable, because there is no risk of
// concurrent modifications.
// Instances of `Ref<T>`, where `T` is non-`const`, are move-only.
// They can be converted to each other with `Ref<T>::Share()` and
// `Ref<const T>::AttemptToClain()`.
template <typename T,
          class Deleter =
              DefaultRefDeleter<typename std::remove_const<T>::type>>
class Ref final
    : public internal::RefBase<typename std::remove_const<T>::type, Deleter,
                               internal::BaseOwnershipTraits<T>::traits> {
 private:
  using Base = internal::RefBase<typename std::remove_const<T>::type, Deleter,
                                 internal::BaseOwnershipTraits<T>::traits>;

 public:
  constexpr explicit Ref(
      Refcounted<typename std::remove_const<T>::type, Deleter> *buffer)
      : Base(buffer) {}

  template <typename U = std::remove_const<T>,
            typename std::enable_if<!std::is_same<U, T>::value>::type>
  Ref(Ref<U, Deleter> &&unique) : Ref(unique.move_buffer()) {}

  Ref(Ref const &other) = default;
  Ref(Ref &&other) = default;

  Ref &operator=(Ref const &other) = default;
  Ref &operator=(Ref &&other) = default;

  friend class Ref<typename std::add_const<T>::type, Deleter>;
  friend class Ref<typename std::remove_const<T>::type, Deleter>;
};

namespace internal {

#ifdef __has_include
#if __has_include(<variant>)
template <typename T, class Deleter>
std::variant<Ref<T, Deleter>, Ref<const T, Deleter>>
RefBase<T, Deleter, OwnershipTraits::shared>::AttemptToClaim() && {
  if (buffer_->refcount.IsOne()) {
    return Ref<T, Deleter>(std::move(*this).move_buffer());
  } else {
    return Ref<const T, Deleter>(std::move(*this).move_buffer());
  }
}
#endif  // __has_include(<variant>)
#endif  // __has_include

template <typename T, class Deleter>
Ref<const T, Deleter> RefBase<T, Deleter, OwnershipTraits::unique>::Share() && {
  return Ref<const T, Deleter>(std::move(*this).move_buffer());
}

}  // namespace internal

template <typename T, typename... Arg>
inline Ref<T, DefaultRefDeleter<T>> New(Arg &&... args) {
  return Ref<T, DefaultRefDeleter<T>>(
      new Refcounted<T, DefaultRefDeleter<T>>(std::forward<Arg>(args)...));
}

}  // namespace refptr

#endif  // _REFCOUNT_STRUCT_H
