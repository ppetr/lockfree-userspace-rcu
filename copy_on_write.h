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

#include "absl/utility/utility.h"
#include "ref.h"

namespace refptr {

// Manages an instance of `T` on the heap. Copying `CopyOnWrite<T>` is as
// cheap as copying a pointer. The actual copying of `T` is deferred until a
// mutable reference is requested by `as_mutable`.
//
// Important: `as_mutable` doesn't return a stable reference. Making a copy of
// the class can cause this reference to change. Therefore it should never be
// exposed externally (unless external callers are aware of this behavior).
template <typename T>
class CopyOnWrite {
  static_assert(std::is_copy_constructible<T>::value,
                "T must be copy-constructible");

 public:
  template <typename... Arg>
  explicit CopyOnWrite(absl::in_place_t, Arg&&... args)
      : ref_(New<T>(std::forward<Arg>(args)...).Share()) {}

  CopyOnWrite(const CopyOnWrite& that) = default;
  CopyOnWrite(CopyOnWrite&& that) = default;
  CopyOnWrite& operator=(const CopyOnWrite&) = default;
  CopyOnWrite& operator=(CopyOnWrite&& that) = default;

  T& as_mutable() {
    auto as_owned = std::move(ref_).AttemptToClaim();
    Ref<T> owned = absl::holds_alternative<Ref<T>>(as_owned)
                       ? absl::get<Ref<T>>(std::move(as_owned))
                       : New<T>(*absl::get<Ref<const T>>(as_owned));
    T& value = *owned;
    ref_ = std::move(owned).Share();
    return value;
  }
  const T& operator*() const { return *ref_; }
  const T* operator->() const { return &this->operator*(); }

 private:
  Ref<const T> ref_;
};

}  // namespace refptr

#endif  // _COPY_ON_WRITE_H