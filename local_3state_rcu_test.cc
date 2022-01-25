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

#include "local_3state_rcu.h"

#include "gtest/gtest.h"

namespace simple_rcu {
namespace {

TEST(Local3StateRcuTest, UpdateAndRead) {
  Local3StateRcu<int> rcu;
  // Set up a new value in `Update()`.
  EXPECT_NE(&rcu.Update(), &rcu.Read())
      << "Update and Read must never point to the same object";
  EXPECT_EQ(rcu.Update(), 0);
  EXPECT_EQ(rcu.Read(), 0);
  rcu.Update() = 42;
  // Trigger.
  EXPECT_FALSE(rcu.TriggerUpdate()) << "Read hasn't advanced yet";
  // Verify expectations before and after `TriggerRead()`.
  EXPECT_NE(&rcu.Update(), &rcu.Read())
      << "Update and Read must never point to the same object";
  EXPECT_EQ(rcu.Update(), 0);
  EXPECT_EQ(rcu.Read(), 0);
  EXPECT_TRUE(rcu.TriggerRead());
  EXPECT_NE(&rcu.Update(), &rcu.Read())
      << "Update and Read must never point to the same object";
  EXPECT_EQ(rcu.Read(), 42);
}

TEST(Local3StateRcuTest, DoubleUpdateBetweenReads) {
  Local3StateRcu<int> rcu;
  rcu.TriggerRead();
  EXPECT_EQ(rcu.Read(), 0);
  EXPECT_EQ(rcu.Update(), 0);
  // Set up a new value in `Update()`.
  rcu.Update() = 42;
  EXPECT_TRUE(rcu.TriggerUpdate()) << "Read should have advanced";
  rcu.Update() = 73;
  EXPECT_FALSE(rcu.TriggerUpdate()) << "Read shouldn't have advance";
  // Verify expectations before and after `TriggerRead()`.
  EXPECT_NE(rcu.Update(), 73)
      << "Update should have overwritten the last value";
  EXPECT_EQ(rcu.Read(), 0);
  EXPECT_TRUE(rcu.TriggerRead());
  EXPECT_EQ(rcu.Read(), 73);
}

TEST(Local3StateRcuTest, DoubleTryUpdateBetweenReads) {
  Local3StateRcu<int> rcu;
  rcu.TriggerRead();
  EXPECT_EQ(rcu.Read(), 0);
  EXPECT_EQ(rcu.Update(), 0);
  // Set up a new value in `Update()`.
  rcu.Update() = 42;
  EXPECT_TRUE(rcu.TryTriggerUpdate()) << "Read should have advanced";
  rcu.Update() = 73;
  EXPECT_FALSE(rcu.TryTriggerUpdate()) << "Read shouldn't have advanced";
  // Verify expectations before and after `TriggerRead()`.
  EXPECT_EQ(rcu.Update(), 73)
      << "Update should not have overwritten the last value";
  EXPECT_EQ(rcu.Read(), 0);
  EXPECT_TRUE(rcu.TriggerRead());
  EXPECT_EQ(rcu.Read(), 42);
  EXPECT_FALSE(rcu.TriggerRead());
  EXPECT_EQ(rcu.Read(), 42);
}

TEST(Local3StateRcuTest, AlternatingUpdatesAndReads) {
  Local3StateRcu<int> rcu;
  rcu.Read() = 1;  // Expected reclaimed value when the loop starts.
  rcu.TriggerRead();
  for (int i = 1; i <= 10; i++) {
    SCOPED_TRACE(i);
    rcu.Update() = -1;  // Value that we'll overwrite later.
    ASSERT_TRUE(rcu.TriggerUpdate()) << "Read should have advanced";
    EXPECT_EQ(rcu.Update(), -(i - 2)) << "Reclaimed value";
    rcu.Update() = i;
    ASSERT_FALSE(rcu.TriggerUpdate())
        << "The second trigger doesn't claim a value from the reader";
    // Read.
    EXPECT_EQ(rcu.Read(), -(i - 1))
        << "Read() should still point to the previous value";
    ASSERT_TRUE(rcu.TriggerRead());
    EXPECT_EQ(rcu.Read(), i) << "Read() should now point to the new value";
    ASSERT_FALSE(rcu.TriggerRead());
    EXPECT_EQ(rcu.Read(), i) << "Read() should still point to the new value";
    rcu.Read() = -i;
  }
}

TEST(Local3StateRcuTest, AlternatingTryUpdatesAndReads) {
  Local3StateRcu<int> rcu;
  rcu.Read() = 1;  // Expected reclaimed value when the loop starts.
  rcu.TriggerRead();
  for (int i = 1; i <= 10; i++) {
    SCOPED_TRACE(i);
    rcu.Update() = i;
    ASSERT_TRUE(rcu.TryTriggerUpdate()) << "Read should have advanced";
    EXPECT_EQ(rcu.Update(), -(i - 2)) << "Reclaimed value";
    rcu.Update() = -1;
    ASSERT_FALSE(rcu.TryTriggerUpdate())
        << "The second try-trigger should have failed";
    // Read.
    EXPECT_EQ(rcu.Read(), -(i - 1))
        << "Read() should still point to the previous value";
    ASSERT_TRUE(rcu.TriggerRead());
    EXPECT_EQ(rcu.Read(), i) << "Read() should now point to the new value";
    ASSERT_FALSE(rcu.TriggerRead());
    EXPECT_EQ(rcu.Read(), i) << "Read() should still point to the new value";
    rcu.Read() = -i;
  }
}

}  // namespace
}  // namespace simple_rcu
