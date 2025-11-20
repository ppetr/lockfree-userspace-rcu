// Copyright 2025 Google LLC
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

#include "simple_rcu/thread_local.h"

#include <memory>

#include "absl/container/flat_hash_map.h"

namespace simple_rcu {

absl::flat_hash_map<std::shared_ptr<void>, std::shared_ptr<void>>
    &InternalPerThreadBase::OwnedMap() {
  static thread_local absl::flat_hash_map<std::shared_ptr<void>,
                                          std::shared_ptr<void>>
      map;
  return map;
}

absl::flat_hash_map<std::shared_ptr<void>,
                    std::unique_ptr<InternalPerThreadBase,
                                    InternalPerThreadBase::MarkAbandoned>>
    &InternalPerThreadBase::NonOwnedMap() {
  static thread_local absl::flat_hash_map<
      std::shared_ptr<void>,
      std::unique_ptr<InternalPerThreadBase,
                      InternalPerThreadBase::MarkAbandoned>>
      map;
  return map;
}

}  // namespace simple_rcu
