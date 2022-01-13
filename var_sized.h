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

// Variable-sized class allocation. Allows to create a new instance of a class
// together with an array in with single memory allocation.

namespace refptr {

// Allocates a given additional number of elements of type `A` to every
// allocated instance(s) of `T`.
template <typename A, typename Alloc, typename T>
class VarAllocator {
  static_assert(!std::is_destructible<A>::value ||
                    std::is_trivially_destructible<A>::value,
                "The array type must be primitive or trivially destructible");

 public:
  using value_type = T;

  explicit VarAllocator(Alloc allocator, size_t size)
      : allocator_(std::move(allocator)), size_(size) {
    static_assert(std::is_trivial<Placeholder>::value,
                  "Internal error: Placeholder class must be trivial");
  }

  template <typename RebindAlloc, typename U>
  VarAllocator(const VarAllocator<A, RebindAlloc, U>& other)
      : allocator_(other.allocator_), size_(other.size_) {}
  template <typename RebindAlloc, typename U>
  VarAllocator(VarAllocator<A, RebindAlloc, U>&& other)
      : allocator_(std::move(other.allocator_)), size_(other.size_) {}

  template <typename RebindAlloc, typename U>
  VarAllocator& operator=(const VarAllocator<A, RebindAlloc, U>& other) {
    allocator_ = other.allocator_;
    size_ = other.size_;
    return *this;
  }
  template <typename RebindAlloc, typename U>
  VarAllocator& operator=(VarAllocator<A, RebindAlloc, U>&& other) {
    allocator_ = std::move(other.allocator_);
    size_ = other.size_;
    return *this;
  }

  T* allocate(size_t length) {
    static_assert(offsetof(Placeholder, node) == 0,
                  "POD first member must be at the 0 offset");
    auto* result =
        reinterpret_cast<T*>(std::allocator_traits<UnitAlloc>::allocate(
            allocator_, AllocatedUnits(/*t_elements=*/length)));
    return result;
  }
  void deallocate(T* ptr, size_t length) {
    std::allocator_traits<UnitAlloc>::deallocate(
        allocator_, reinterpret_cast<Unit*>(ptr),
        AllocatedUnits(/*t_elements=*/length));
  }

  A* GetArray(T* ptr, size_t length) const {
    return reinterpret_cast<A*>(
        &reinterpret_cast<Placeholder*>(ptr + (length - 1))->array);
  }

  size_t GetSize() const { return size_; }

  template <typename U>
  struct rebind {
    using other = VarAllocator<A, Alloc, U>;
  };

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
  using Unit = typename std::aligned_storage<1, alignof(Placeholder)>::type;
  using UnitAlloc =
      typename std::allocator_traits<Alloc>::template rebind_alloc<Unit>;

  static constexpr size_t AllocatedUnits(size_t a_elements, size_t t_elements) {
    return (sizeof(T) * (t_elements - 1) + sizeof(Placeholder) +
            (a_elements - 1) * sizeof(A) + sizeof(Unit) - 1) /
           sizeof(Unit);
  }

  size_t AllocatedUnits(size_t t_elements) const {
    return AllocatedUnits(size_, t_elements);
  }

  UnitAlloc allocator_;
  size_t size_;

  template <typename B, typename BAlloc, typename U>
  friend class VarAllocator;
};

// Owns a block of memory large enough to store a properly aligned instance of
// `T` and additional `size` number of elements of type `A`.
template <typename T, typename A, typename Alloc>
class VarAllocation {
 public:
  // Calls `~T` and deletes the corresponding `VarAllocation`.
  // Doesn't delete the `A[]`, therefore it must be a primitive type or a
  // trivially destructible one.
  class Deleter {
   public:
    void operator()(T* to_delete) {
      VarAllocation<T, A, Alloc> deleter(to_delete, std::move(*this));
      deleter.Node()->~T();
    }

   private:
    Deleter(VarAllocator<A, Alloc, T> allocator)
        : allocator_(std::move(allocator)) {}

    VarAllocator<A, Alloc, T> allocator_;

    friend class VarAllocation<T, A, Alloc>;
  };

  // Allocates memory for a properly aligned instance of `T`, plus additional
  // array of `size` elements of `A`.
  explicit VarAllocation(size_t size, Alloc allocator = {})
      : deleter_(VarAllocator<A, Alloc, T>(std::move(allocator), size)),
        node_(std::allocator_traits<VarAllocator<A, Alloc, T>>::allocate(
            deleter_.allocator_, 1)) {}
  VarAllocation(VarAllocation const&) = delete;
  VarAllocation(VarAllocation&& other) {
    node_ = other.node_;
    other.node_ = nullptr;
    deleter_ = std::move(other.deleter_);
  }

  ~VarAllocation() {
    if (node_) {
      std::allocator_traits<VarAllocator<A, Alloc, T>>::deallocate(
          deleter_.allocator_, node_, 1);
    }
  }

  // Returns a pointer to an uninitialized memory area available for an
  // instance of `T`.
  T* Node() const { return node_; }
  // Returns a pointer to an uninitialized memory area available for
  // holding `size` (specified in the constructor) elements of `A`.
  A* Array() const { return deleter_.allocator_.GetArray(node_, 1); }

  size_t Size() const { return deleter_.allocator_.GetSize(); }

  // Constructs a deleter for this particular `VarAllocation`.
  // If used with a different instance, the behaivor is undefined.
  Deleter ToDeleter() && {
    node_ = nullptr;
    return std::move(deleter_);
  }

 private:
  // Creates a placement from its building blocks.
  //
  // - `ptr` must be a pointer previously obtained from `VarAllocation::Node`.
  // - `deleter` must be a value previously obtained by
  //   `VarAllocation::ToDeleter`.
  VarAllocation(T* node, Deleter&& deleter)
      : deleter_(std::move(deleter)), node_(node) {}

  Deleter deleter_;
  T* node_;
};

// Constructs a new instance of `U` in-place using the given arguments, with an
// additional block of memory of `B[length]`, with a single memory allocation.
// A `B*` pointer to this buffer and its `size_t` length are passed as the
// first two arguments to the constructor of `U`.
template <typename U, typename B, typename... Arg,
          typename Alloc = std::allocator<B>>
inline std::unique_ptr<U, typename VarAllocation<U, B, Alloc>::Deleter>
MakeUnique(size_t length, B*& varsized, Arg&&... args) {
  VarAllocation<U, B, Alloc> placement(length);
  auto* node = placement.Node();
  varsized = new (placement.Array()) B[length];
  auto* value = new (node) U(std::forward<Arg>(args)...);
  return std::unique_ptr<U, typename VarAllocation<U, B, Alloc>::Deleter>(
      value, std::move(placement).ToDeleter());
}

template <typename U, typename B, typename... Arg,
          typename Alloc = std::allocator<B>>
inline std::shared_ptr<U> MakeShared(size_t length, B*& varsized, Arg&&... args,
                                     Alloc alloc = {}) {
  VarAllocator<B, Alloc, U> var_alloc(std::move(alloc), length);
  std::shared_ptr<U> shared =
      std::allocate_shared<U, VarAllocator<B, Alloc, U>, Arg...>(
          var_alloc, std::forward<Arg>(args)...);
  varsized = new (var_alloc.Array(shared.get(), 1)) B[length];
  return shared;
}

}  // namespace refptr

#endif  // _VAR_SIZED_H
