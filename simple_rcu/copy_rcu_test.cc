// Copyright 2022-2023 Google LLC
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

#include <functional>
#include <memory>

#include "simple_rcu/copy_rcu.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace simple_rcu {
namespace {

using ::testing::Pointee;

TEST(CopyRcuTest, UpdateAndRead) {
  CopyRcu<int> rcu;
  CopyRcu<int>::View local1(rcu);
  rcu.Update(42);
  CopyRcu<int>::View local2(rcu);
  EXPECT_THAT(local1.Read(), Pointee(42))
      << "Thread registered prior Update must receive the value";
  EXPECT_THAT(local2.Read(), Pointee(42))
      << "Thread registered after Update must also receive the value";
  EXPECT_NE(local1.Read(), local2.Read())
      << "Each snapshot must be a different (local) pointer";
}

TEST(CopyRcuTest, UpdateAndReadConstRef) {
  // Also tests that it works with a type that is not default-constructible.
  const int old_value = 0;
  CopyRcu<const std::reference_wrapper<const int>> rcu(old_value);
  CopyRcu<const std::reference_wrapper<const int>>::View local(rcu);
  const int value = 42;
  rcu.Update(value);
  EXPECT_THAT(local.Read(), Pointee(42))
      << "Reader thread must receive a correct value";
}

TEST(CopyRcuTest, UpdateIf) {
  CopyRcu<int> rcu(0);
  CopyRcu<int>::View local(rcu);
  rcu.UpdateIf(42, [](int previous) { return previous != 0; });
  EXPECT_THAT(local.Read(), Pointee(0))
      << "Must not update a value that doesn't match the predicate";
  rcu.UpdateIf(42, [](const int& previous) { return previous == 0; });
  EXPECT_THAT(local.Read(), Pointee(42))
      << "Must update a value that matches the predicate";
}

TEST(CopyRcuTest, ReadRemainsStable) {
  CopyRcu<int> rcu(42);
  CopyRcu<int>::View local(rcu);
  auto read_ref1 = local.Read();
  rcu.Update(73);
  EXPECT_THAT(read_ref1, Pointee(42))
      << "The first reference must hold its value past Update()";
  auto read_ref2 = local.Read();
  EXPECT_THAT(read_ref1, Pointee(42))
      << "The first reference must hold its value past a nested Read()";
  EXPECT_THAT(read_ref2, Pointee(42))
      << "A nested Read() must hold the same value as an outer one";
  EXPECT_EQ(read_ref1.get(), read_ref2.get())
      << "A nested Read() must point to the same value as an outer one";
}

TEST(RcuTest, UpdateAndReadPtr) {
  Rcu<int> rcu;
  Rcu<int>::View local1(rcu);
  EXPECT_EQ(local1.ReadPtr(), nullptr);
  rcu.Update(std::make_shared<int>(42));
  Rcu<int>::View local2(rcu);
  EXPECT_THAT(local1.ReadPtr(), Pointee(42))
      << "Thread registered prior Update must receive the value";
  EXPECT_THAT(local2.ReadPtr(), Pointee(42))
      << "Thread registered after Update must also receive the value";
  EXPECT_EQ(local1.ReadPtr(), local2.ReadPtr())
      << "Both pointer snapshots must point to the shared value";
}

TEST(CopyRcuTest, ThreadLocalUpdateAndRead) {
  {
    const auto rcu = std::make_shared<Rcu<int>>();
    EXPECT_EQ(*Read(rcu), nullptr);
    EXPECT_EQ(ReadPtr(rcu), nullptr);
    rcu->Update(std::make_shared<int>(42));
    EXPECT_THAT(Read(rcu), Pointee(Pointee(42)));
    EXPECT_THAT(ReadPtr(rcu), Pointee(42));
    EXPECT_EQ(Rcu<int>::CleanUpThreadLocal(), 0);
  }
  EXPECT_EQ(Rcu<int>::CleanUpThreadLocal(), 1);
  EXPECT_EQ(Rcu<int>::CleanUpThreadLocal(), 0);
}

}  // namespace
}  // namespace simple_rcu
