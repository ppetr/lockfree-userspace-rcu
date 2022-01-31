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

#include "simple_rcu/rcu.h"

#include "gtest/gtest.h"

namespace simple_rcu {
namespace {

TEST(RcuTest, UpdateAndRead) {
  Rcu<int> rcu;
  Rcu<int>::Local local1(rcu);
  rcu.Update(42);
  Rcu<int>::Local local2(rcu);
  EXPECT_EQ(*local1.Read(), 42)
      << "Thread registered prior Update must receive the value";
  EXPECT_EQ(*local2.Read(), 42)
      << "Thread registered after Update must also receive the value";
}

TEST(RcuTest, UpdateAndReadConst) {
  Rcu<int const> rcu;
  Rcu<int const>::Local local(rcu);
  rcu.Update(42);
  EXPECT_EQ(*local.Read(), 42) << "Reader thread must receive a correct value";
}

TEST(RcuTest, ThreadLocalUpdateAndRead) {
  static Rcu<int> rcu;
  static thread_local Rcu<int>::Local local(rcu);
  rcu.Update(42);
  EXPECT_EQ(*local.Read(), 42) << "Thread-local must receive the value";
}

TEST(RcuTest, ReadRemainsStable) {
  Rcu<int> rcu(42);
  Rcu<int>::Local local(rcu);
  auto read_ref1 = local.Read();
  rcu.Update(73);
  EXPECT_EQ(*read_ref1, 42)
      << "The first reference must hold its value past Update()";
  auto read_ref2 = local.Read();
  EXPECT_EQ(*read_ref1, 42)
      << "The first reference must hold its value past another Read()";
  EXPECT_EQ(*read_ref2, 42)
      << "A nested reference must have the same value as an outer one";
}

}  // namespace
}  // namespace simple_rcu
