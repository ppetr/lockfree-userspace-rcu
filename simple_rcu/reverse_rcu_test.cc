// Copyright 2022 Google LLC
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

#include "simple_rcu/reverse_rcu.h"

#include <memory>

#include "gtest/gtest.h"

namespace simple_rcu {
namespace {

TEST(ReverseRcuTest, WriteAndCollect) {
  ReverseRcu<int> rcu;
  ReverseRcu<int>::View local1(rcu);
  {
    ReverseRcu<int>::View local2(rcu);
    *local2.Write() += 10;
  }
  *local1.Write() += 1;
  EXPECT_EQ(rcu.Collect(), 11)
      << "Should receive value from both live and terminated threads";
}

TEST(ReverseRcuTest, WriteAndCollectMoveable) {
  struct Value {
    Value() : value(0) {}
    Value(Value&&) = default;
    Value(const Value&) = delete;
    Value& operator=(Value&&) = default;
    Value& operator=(const Value&) = delete;

    Value& operator+=(Value&& other) {
      value += other.value;
      return *this;
    }

    int value;
  };
  ReverseRcu<Value> rcu;
  ReverseRcu<Value>::View local(rcu);
  local.Write()->value += 42;
  EXPECT_EQ(rcu.Collect().value, 42) << "Should receive a moved value";
}

TEST(ReverseRcuTest, ThreadLocalWriteAndCollect) {
  static ReverseRcu<int> rcu;
  static thread_local ReverseRcu<int>::View local(rcu);
  *local.Write() += 42;
  EXPECT_EQ(rcu.Collect(), 42) << "Thread-local must receive the value";
}

TEST(ReverseRcuTest, WriteRemainsStable) {
  ReverseRcu<int> rcu;
  ReverseRcu<int>::View local(rcu);
  auto write_ref1 = local.Write();
  *write_ref1 = 42;
  EXPECT_EQ(rcu.Collect(), 0) << "The value should not be collected yet";
  EXPECT_EQ(*write_ref1, 42)
      << "The first reference must hold its value past Collect()";
  {
    auto write_ref2 = local.Write();
    EXPECT_EQ(*write_ref2, 42)
        << "A nested reference must have the same value as an outer one";
  }
  EXPECT_EQ(*write_ref1, 42) << "The first reference must hold its value past "
                                "another Write() destructor";
  EXPECT_EQ(rcu.Collect(), 0) << "The value should not be collected still";
}

}  // namespace
}  // namespace simple_rcu
