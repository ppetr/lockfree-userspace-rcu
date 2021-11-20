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

#include "var_sized.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace refptr {
namespace {

// Copies `source` to `target` and returns `target` as an `absl::string_view`.
absl::string_view CopyTo(const char* source, char* target, size_t target_size) {
  strncpy(target, source, target_size);
  target[target_size - 1] = '\0';
  return absl::string_view(target);
}

struct Foo {
 public:
  Foo(char* buffer, size_t buffer_size, int& counter, const char* source)
      : counter_(counter), buffer(CopyTo(source, buffer, buffer_size)) {
    counter_++;
  }
  ~Foo() { counter_--; }

  int& counter_;
  const absl::string_view buffer;
};

class VarSizedTest : public testing::Test {
 protected:
  VarSizedTest() : counter_(0) {}

  void TearDown() override { EXPECT_EQ(counter_, 0); }

  int counter_;
};

TEST_F(VarSizedTest, MakeUniqueWorks) {
  auto owned = MakeUnique<Foo, char, int&, const char*>(
      16, counter_, "Lorem ipsum dolor sit amet");
  EXPECT_EQ(counter_, 1);
  EXPECT_EQ(absl::string_view(owned->buffer), "Lorem ipsum dol");
}

TEST_F(VarSizedTest, UniqueConvertsToSharedPtr) {
  auto owned = MakeUnique<Foo, char, int&, const char*>(
      16, counter_, "Lorem ipsum dolor sit amet");
  std::shared_ptr<Foo> shared(std::move(owned));
  ASSERT_FALSE(owned);
  EXPECT_EQ(counter_, 1);
  EXPECT_EQ(shared->buffer, "Lorem ipsum dol");
}

TEST_F(VarSizedTest, MakeRefCountedWorks) {
  auto owned = MakeRefCounted<Foo, char, int&, const char*>(
      16, counter_, "Lorem ipsum dolor sit amet");
  EXPECT_EQ(counter_, 1);
  EXPECT_EQ(absl::string_view(owned->buffer), "Lorem ipsum dol");
}

TEST_F(VarSizedTest, RefCountedConvertsToShared) {
  auto owned = MakeRefCounted<Foo, char, int&, const char*>(
      16, counter_, "Lorem ipsum dolor sit amet");
  auto shared = std::move(owned).Share();
  EXPECT_EQ(counter_, 1);
  EXPECT_EQ(shared->buffer, "Lorem ipsum dol");
}

}  // namespace
}  // namespace refptr
