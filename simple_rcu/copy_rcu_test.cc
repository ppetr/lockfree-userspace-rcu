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

#include "simple_rcu/copy_rcu.h"

#include <functional>
#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace simple_rcu {
namespace {

using ::testing::Eq;
using ::testing::Pair;
using ::testing::Pointee;

TEST(CopyRcuTest, UpdateAndSnapshot) {
  CopyRcu<int> rcu;
  rcu.Snapshot();
  rcu.Update(42);
  EXPECT_THAT(rcu.Snapshot(), Eq(42))
      << "Thread registered prior Update should receive the value";
}

TEST(CopyRcuTest, UpdateAndSnapshotRef) {
  CopyRcu<int> rcu;
  rcu.Snapshot();
  rcu.Update(42);
  CopyRcu<int>::View& view = rcu.ThreadLocalView();
  EXPECT_THAT(view.SnapshotRef(), Pair(Eq(42), true))
      << "Should receive a reference with the correct value marked as new";
  EXPECT_THAT(view.SnapshotRef(), Pair(Eq(42), false))
      << "Should receive a reference with the correct value marked as old";
  EXPECT_THAT(view.SnapshotRef(), Pair(Eq(42), false))
      << "Should receive a reference with the correct value marked as old";
}

TEST(CopyRcuTest, UpdateAndSnapshotAfter) {
  CopyRcu<int> rcu;
  rcu.Update(42);
  EXPECT_THAT(rcu.Snapshot(), Eq(42))
      << "Thread registered after Update should also receive the value";
}

TEST(CopyRcuTest, UpdateAndSnapshotConstRef) {
  // Also tests that it works with a type that is not default-constructible.
  const int old_value = 0;
  CopyRcu<const std::reference_wrapper<const int>> rcu(old_value);
  EXPECT_THAT(rcu.Snapshot(), Eq(0));
  const int value = 42;
  rcu.Update(value);
  EXPECT_THAT(rcu.Snapshot(), Eq(42));
}

TEST(CopyRcuTest, UpdateIf) {
  CopyRcu<int> rcu(0);
  rcu.UpdateIf(42, [](int previous) { return previous != 0; });
  EXPECT_THAT(rcu.Snapshot(), Eq(0))
      << "Should not update a value that doesn't match the predicate";
  rcu.UpdateIf(42, [](const int& previous) { return previous == 0; });
  EXPECT_THAT(rcu.Snapshot(), Eq(42))
      << "Should update a value that matches the predicate";
}

TEST(RcuTest, UpdateAndSnapshotPtr) {
  Rcu<int> rcu;
  EXPECT_EQ(rcu.Snapshot(), nullptr);
  rcu.Update(std::make_shared<int>(42));
  EXPECT_THAT(rcu.Snapshot(), Pointee(42));
}

TEST(RcuTest, EraseDestroys) {
  Rcu<int> rcu;
  rcu.Update(std::make_shared<int>(73));
  std::shared_ptr<const int> ptr = rcu.ThreadLocalView().SnapshotRef().first;
  const auto count_before = ptr.use_count();
  rcu.erase();
  EXPECT_LT(ptr.use_count(), count_before);
}

}  // namespace
}  // namespace simple_rcu
