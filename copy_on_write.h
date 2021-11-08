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

#ifndef _COPY_ON_WRITE_H
#define _COPY_ON_WRITE_H

#include <type_traits>
#include <utility>

#include "refcount.h"

namespace refptr {

// Manages an instance of `T` on the heap. Copying `CopyOnWrite<T>` is as
// cheap as copying a pointer. The actual copying of `T` is deferred until a
// mutable reference is requested by `as_mutable`.
template <typename T>
class CopyOnWrite {
  static_assert(std::is_copy_constructible_v<T>);

 public:
  CopyOnWrite(const CopyOnWrite& that) : refcounted_(that.refcounted_) {
    refcounted_->refcount.Inc();
  }

  template <typename... Arg>
  CopyOnWrite(std::in_place_t, Arg&&... args)
      : refcounted_(new Refcounted<T>(std::forward<Arg>(args)...)) {}

  ~CopyOnWrite() {
    if (refcounted_->refcount.Dec()) {
      std::move(*refcounted_).SelfDelete();
    }
  }

  T& as_mutable() {
    if (refcounted_->refcount.IsOne()) {
      return refcounted_->nested;
    }
    Refcounted<T>& old = *refcounted_;
    refcounted_ = new Refcounted<T>(refcounted_->nested);
    if (old.refcount.Dec()) {
      std::move(old).SelfDelete();
    }
    return refcounted_->nested;
  }
  const T& operator*() const { return refcounted_->nested; }
  T* operator->() { return &this->operator*(); }

 private:
  mutable Refcounted<T>* refcounted_;
};

}  // namespace refptr

#endif  // _COPY_ON_WRITE_H
