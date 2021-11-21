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

#include "copy_on_write.h"

#include <string>

#include "absl/strings/string_view.h"
#include "absl/utility/utility.h"
#include "gtest/gtest.h"

namespace refptr {
namespace {

constexpr absl::string_view kText = "Lorem ipsum dolor sit amet";

TEST(CopyOnWriteTest, ConstructsInPlace) {
  CopyOnWrite<std::string> cow(absl::in_place, kText);
  EXPECT_EQ(*cow, kText);
  EXPECT_FALSE(cow->empty());  // Test operator->.
  EXPECT_EQ(cow.as_mutable(), kText);
}

TEST(CopyOnWriteTest, Moves) {
  CopyOnWrite<std::string> original(absl::in_place, kText);
  CopyOnWrite<std::string> cow = std::move(original);
  EXPECT_EQ(*cow, kText);
  EXPECT_EQ(cow.as_mutable(), kText);
}

TEST(CopyOnWriteTest, Copies) {
  CopyOnWrite<std::string> original(absl::in_place, kText);
  CopyOnWrite<std::string> cow = original;
  // Original.
  EXPECT_EQ(*original, kText);
  EXPECT_EQ(original.as_mutable(), kText);
  // Copy.
  EXPECT_EQ(*cow, kText);
  EXPECT_EQ(cow.as_mutable(), kText);
}

}  // namespace
}  // namespace refptr
