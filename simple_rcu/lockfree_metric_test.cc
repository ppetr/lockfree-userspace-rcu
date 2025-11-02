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

#include "simple_rcu/lockfree_metric.h"

#include <cstdint>
#include <iterator>
#include <thread>

#include "absl/random/random.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace simple_rcu {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Optional;

TEST(LocalLockFreeMetricTest, ChangeSeenImmediately) {
  LocalLockFreeMetric<int_least32_t> metric;
  EXPECT_THAT(metric.Collect(), IsEmpty());
  metric.Update(1);
  EXPECT_THAT(metric.Collect(), ElementsAre(1));
  EXPECT_THAT(metric.Collect(), IsEmpty());
  // Another round.
  metric.Update(2);
  metric.Update(3);
  EXPECT_THAT(metric.Collect(), ElementsAre(2, 3));
  EXPECT_THAT(metric.Collect(), IsEmpty());
}

namespace {

template <typename T>
void AppendTo(std::deque<T>&& input, std::deque<T>& target) {
  target.insert(target.end(), std::make_move_iterator(input.begin()),
                std::make_move_iterator(input.end()));
}

}  // namespace

TEST(LocalLockFreeMetricTest, TwoThreads) {
  static constexpr int_least32_t kCount = 0x10000;
  absl::BitGen bitgen;
  LocalLockFreeMetric<int_least32_t> metric;
  absl::Notification updater_done;
  std::thread updater([&]() {
    for (int_least32_t i = 0; i < kCount; i++) {
      metric.Update(i);
      absl::SleepFor(absl::Nanoseconds(absl::Uniform(bitgen, 0, 1000)));
    }
    updater_done.Notify();
  });
  std::deque<int_least32_t> result;
  while (!updater_done.HasBeenNotified()) {
    AppendTo(metric.Collect(), result);
    absl::SleepFor(absl::Nanoseconds(absl::Uniform(bitgen, 0, 1000)));
  }
  updater.join();
  AppendTo(metric.Collect(), result);
  ASSERT_EQ(result.size(), kCount);
  for (int_least32_t i = 0; i < kCount; i++) {
    ASSERT_EQ(i, result[i]);
  }
}

}  // namespace
}  // namespace simple_rcu
