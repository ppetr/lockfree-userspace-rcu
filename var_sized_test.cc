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

#include <memory>

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace refptr {
namespace {

constexpr absl::string_view kLoremIpsum = "Lorem ipsum dolor sit amet";

// Copies `source` to `target` and returns `target` as an `absl::string_view`.
absl::string_view CopyTo(absl::string_view source, char* target,
                         size_t target_size) {
  auto rcount = source.copy(target, target_size);
  return absl::string_view(target, rcount);
}

struct Foo {
 public:
  Foo(int& counter) : counter_(counter) { counter_++; }
  // The constructor is intentionally virtual to make the class non-trivial.
  virtual ~Foo() { counter_--; }

  int& counter_;
};

class VarSizedTest : public testing::Test {
 protected:
  VarSizedTest() : counter_(0) {}

  void TearDown() override { EXPECT_EQ(counter_, 0); }

  int counter_;
};

TEST_F(VarSizedTest, MakeUniqueWorks) {
  char* array;
  auto owned = MakeUnique<Foo, char, int&>(16, array, counter_);
  auto copied = CopyTo(kLoremIpsum, array, 16);
  EXPECT_EQ(counter_, 1);
  EXPECT_EQ(copied, "Lorem ipsum dolo");
  owned = nullptr;
  EXPECT_EQ(counter_, 0);
}

TEST_F(VarSizedTest, UniqueConvertsToSharedPtr) {
  char* array;
  auto owned = MakeUnique<Foo, char, int&>(16, array, counter_);
  std::shared_ptr<Foo> shared(std::move(owned));
  ASSERT_FALSE(owned);
  EXPECT_EQ(counter_, 1);
  shared = nullptr;
  EXPECT_EQ(counter_, 0);
}

TEST_F(VarSizedTest, MakeSharedWorks) {
  char* array;
  auto shared = MakeShared<Foo, char, int&>(16, array, counter_);
  auto copied = CopyTo(kLoremIpsum, array, 16);
  EXPECT_EQ(counter_, 1);
  EXPECT_EQ(copied, "Lorem ipsum dolo");
  shared = nullptr;
  EXPECT_EQ(counter_, 0);
}

}  // namespace
}  // namespace refptr
