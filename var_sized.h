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

#ifndef _VAR_SIZED_H
#define _VAR_SIZED_H

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

#include "ref.h"
#include "reference_counted.h"

// Variable-sized class allocation. Allows to create a new instance of a class
// together with an array in with single memory allocation.

namespace refptr {

// Owns a block of memory large enough to store a properly aligned instance of
// `T` and additional `size` number of elements of type `A`.
template <typename T, typename A>
class VarAllocation {
 public:
  // Calls `~T` and deletes the corresponding `VarAllocation`.
  // Doesn't delete the `A[]`, therefore it must be a primitive type or a
  // trivially destructible one.
  class Deleter {
   public:
    static_assert(!std::is_destructible<A>::value ||
                      std::is_trivially_destructible<A>::value,
                  "The array type must be primitive or trivially destructible");

    void operator()(T *to_delete) {
      VarAllocation<T, A> deleter(to_delete, std::move(*this));
      deleter.Node()->~T();
    }

   private:
    Deleter(size_t size) : size_(size) {}

    size_t size_;

    friend class VarAllocation<T, A>;
  };

  // Allocates memory for a properly aligned instance of `T`, plus additional
  // array of `size` elements of `A`.
  explicit VarAllocation(size_t size)
      : deleter_(size),
        allocation_(std::allocator<Unit>().allocate(AllocatedUnits())) {
    static_assert(std::is_trivial<Placeholder>::value,
                  "Internal error: Placeholder class must be trivial");
  }
  VarAllocation(VarAllocation const &) = delete;
  VarAllocation(VarAllocation &&other) {
    allocation_ = other.allocation_;
    other.allocation_ = nullptr;
    deleter_ = std::move(other.deleter_);
  }

  ~VarAllocation() {
    if (allocation_) {
      std::allocator<Unit>().deallocate(static_cast<Unit *>(allocation_),
                                        AllocatedUnits());
    }
  }

  // Returns a pointer to an uninitialized memory area available for an
  // instance of `T`.
  T *Node() const { return reinterpret_cast<T *>(&AsPlaceholder()->node); }
  // Returns a pointer to an uninitialized memory area available for
  // holding `size` (specified in the constructor) elements of `A`.
  A *Array() const { return reinterpret_cast<A *>(&AsPlaceholder()->array); }

  size_t Size() const { return deleter_.size_; }

  // Constructs a deleter for this particular `VarAllocation`.
  // If used with a different instance, the behaivor is undefined.
  Deleter ToDeleter() && {
    allocation_ = nullptr;
    return std::move(deleter_);
  }

 private:
  // Holds a properly aligned instance of `T` and an array of length 1 of `A`.
  struct Placeholder {
    typename std::aligned_storage<sizeof(T), alignof(T)>::type node;
    // The array type must be the last one in the struct.
    typename std::aligned_storage<sizeof(A[1]), alignof(A[1])>::type array;
  };
  // Properly aligned unit used for the actual allocation.
  // It can occupy more than 1 byte, therefore we need to properly compute
  // their required number below.
  struct Unit {
    typename std::aligned_storage<1, alignof(Placeholder)>::type _;
  };

  // Creates a placement from its building blocks.
  //
  // - `ptr` must be a pointer previously obtained from `VarAllocation::Node`.
  // - `deleter` must be a value previously obtained by
  //   `VarAllocation::ToDeleter`.
  VarAllocation(T *ptr, Deleter &&deleter);

  constexpr size_t AllocatedUnits() {
    return (sizeof(Placeholder) + (Size() - 1) * sizeof(A) + sizeof(A) - 1) /
           sizeof(A);
  }

  Placeholder *AsPlaceholder() const {
    return static_cast<Placeholder *>(allocation_);
  }

  Deleter deleter_;
  void *allocation_;
};

template <typename T, typename A>
VarAllocation<T, A>::VarAllocation(T *ptr, Deleter &&deleter)
    : deleter_(std::move(deleter)),
      allocation_(reinterpret_cast<char *>(ptr) - offsetof(Placeholder, node)) {
}

// Constructs a new instance of `U` in-place using the given arguments, with an
// additional block of memory of `B[length]`, with a single memory allocation.
// A `B*` pointer to this buffer and its `size_t` length are passed as the
// first two arguments to the constructor of `U`.
template <typename U, typename B, typename... Arg>
inline std::unique_ptr<U, typename VarAllocation<U, B>::Deleter> MakeUnique(
    size_t length, Arg &&... args) {
  VarAllocation<U, B> placement(length);
  auto *node = placement.Node();
  auto *array = new (placement.Array()) B[length];
  auto *value = new (node) U(array, length, std::forward<Arg>(args)...);
  return std::unique_ptr<U, typename VarAllocation<U, B>::Deleter>(
      value, std::move(placement).ToDeleter());
}

// Deleter for reference counted, variable-sized structures of type `T`.
template <typename T, typename A>
class VarRefDeleter {
 public:
  using RefType = Refcounted<T, VarRefDeleter<T, A>>;

  // Takes ownership of a `VarAllocation` to be deleted by `Delete`.
  VarRefDeleter(VarAllocation<RefType, A> &&placement)
      : deleter_(std::move(placement).ToDeleter()) {}

  // Runs the destructor of `*to_delete` and deallocates its `VarAllocation`.
  void operator()(RefType *to_delete) { deleter_(to_delete); }

 private:
  typename VarAllocation<RefType, A>::Deleter deleter_;
};

// Similar to `MakeUnique` above, also with a single memory allocation, with
// the difference that it creates a reference counted value to allow efficient
// and type-safe sharing of the construted value.
template <typename U, typename B, typename... Arg>
inline Ref<U, VarRefDeleter<U, B>> MakeRefCounted(size_t length,
                                                  Arg &&... args) {
  using RefType = Refcounted<U, VarRefDeleter<U, B>>;
  VarAllocation<RefType, B> placement(length);
  auto *node = placement.Node();
  auto *array = new (placement.Array()) B[length];
  auto *value = new (node) RefType(VarRefDeleter<U, B>(std::move(placement)),
                                   array, length, std::forward<Arg>(args)...);
  return Ref<U, VarRefDeleter<U, B>>(value);
}

}  // namespace refptr

#endif  // _VAR_SIZED_H
