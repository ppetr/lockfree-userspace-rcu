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

#include "ref.h"

#include "gtest/gtest.h"

namespace refptr {
namespace {

struct Foo {
 public:
  Foo(int& counter, int value) : counter_(counter), value_(value) {
    counter_++;
  }
  // The constructor is intentionally virtual to make the class non-trivial.
  virtual ~Foo() { counter_--; }

  int& counter_;
  int value_;
};

class RefTest : public testing::Test {
 protected:
  RefTest() : counter_(0) {}

  void TearDown() override { EXPECT_EQ(counter_, 0); }

  int counter_;
};

TEST_F(RefTest, ConstructionAndAssignmentWorks) {
  Ref<Foo> owned(New<Foo, int&, int>(counter_, 42));
  EXPECT_EQ(counter_, 1);
  EXPECT_EQ(owned->value_, 42);
  Ref<Foo> other = std::move(owned);
  EXPECT_EQ(counter_, 1);
  EXPECT_EQ(other->value_, 42);
}

TEST_F(RefTest, Share) {
  Ref<Foo> owned = New<Foo, int&, int>(counter_, 42);
  Ref<const Foo> shared(std::move(owned).Share());
  EXPECT_EQ(counter_, 1);
  EXPECT_EQ(shared->value_, 42);
}

TEST_F(RefTest, AttemptToClaimSucceeds) {
  Ref<const Foo> shared = New<Foo, int&, int>(counter_, 42).Share();
  auto owned_var = std::move(shared).AttemptToClaim();
  EXPECT_EQ(counter_, 1);
  ASSERT_TRUE(absl::holds_alternative<Ref<Foo>>(owned_var));
  EXPECT_EQ(absl::get<Ref<Foo>>(owned_var)->value_, 42);
}

TEST_F(RefTest, AttemptToClaimFails) {
  Ref<const Foo> shared = New<Foo, int&, int>(counter_, 42).Share();
  Ref<const Foo> shared2 = shared;
  EXPECT_EQ(counter_, 1);
  auto owned_var = std::move(shared).AttemptToClaim();
  EXPECT_EQ(counter_, 1);
  ASSERT_TRUE(absl::holds_alternative<Ref<const Foo>>(owned_var));
  EXPECT_EQ(absl::get<Ref<const Foo>>(owned_var)->value_, 42);
}

}  // namespace
}  // namespace refptr
