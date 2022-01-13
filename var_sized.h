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

  // Returns an uninitialized area of memory co-allocated by a previous call to
  // `allocate(length)`, that is suitable for holding `GetSize()` elements of
  // type `A`.
  A* Array(T* ptr, size_t length) const {
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

// Destroys and deallocates an instance of `U` using `Alloc`.
template <typename Alloc>
struct AllocDeleter {
  Alloc allocator;

  using pointer = typename std::allocator_traits<Alloc>::pointer;

  void operator()(pointer to_delete) {
    std::allocator_traits<Alloc>::destroy(allocator, to_delete);
    std::allocator_traits<Alloc>::deallocate(allocator, to_delete, 1);
  }
};

// Constructs a new instance of `U` in-place using the given arguments, with an
// additional block of memory of `B[length]`, with a single memory allocation.
// A `B*` pointer to this buffer and its `size_t` length are passed as the
// first two arguments to the constructor of `U`.
template <typename U, typename B, typename... Arg,
          typename Alloc = std::allocator<B>>
inline std::unique_ptr<U, AllocDeleter<VarAllocator<B, Alloc, U>>> MakeUnique(
    size_t length, B*& varsized, Arg&&... args, Alloc alloc = {}) {
  VarAllocator<B, Alloc, U> var_alloc(std::move(alloc), length);
  U* node =
      std::allocator_traits<VarAllocator<B, Alloc, U>>::allocate(var_alloc, 1);
  try {
    std::allocator_traits<VarAllocator<B, Alloc, U>>::construct(
        var_alloc, node, std::forward<Arg>(args)...);
    varsized = new (var_alloc.Array(node, 1)) B[length];
  } catch (...) {
    std::allocator_traits<VarAllocator<B, Alloc, U>>::deallocate(var_alloc,
                                                                 node, 1);
    throw;
  }
  return std::unique_ptr<U, AllocDeleter<VarAllocator<B, Alloc, U>>>(
      node, {.allocator = std::move(var_alloc)});
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
