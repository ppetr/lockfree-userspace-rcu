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

#include "simple_rcu/lock_free_metric.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
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
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Optional;

template <typename C>
struct BackCollection {
  using value_type = typename C::value_type;

  BackCollection() = default;
  BackCollection(BackCollection const&) = default;
  BackCollection(BackCollection&&) = default;
  BackCollection& operator=(BackCollection const&) = default;
  BackCollection& operator=(BackCollection&&) = default;

  BackCollection& operator+=(const value_type& value) {
    collection.emplace_back(value);
    return *this;
  }
  BackCollection& operator+=(value_type&& value) {
    collection.push_back(std::move(value));
    return *this;
  }

  C collection;
};

// Wraps `C` to be acted upon by an arbitrary functor using
// `operator+=`. This allows metric-like collection of changes on
// any default-constructed `C` objects such as:
//
//   LockFreeMetric<AnyFunctor<C>, std::function<void(C&)>>
//
// See test `ArbitraryFunctor` below.
template <typename C>
struct AnyFunctor {
  using diff_type = std::function<void(C&)>;

  template <typename F>
  AnyFunctor& operator+=(F&& f) {
    std::forward<F>(f)(value);
    return *this;
  }

  C value;
};

template <typename T>
void AppendTo(std::deque<T>&& input, std::deque<T>& target) {
  target.insert(target.end(), std::make_move_iterator(input.begin()),
                std::make_move_iterator(input.end()));
}

TEST(LocalLockFreeMetricTest, ChangeSeenImmediatelyInt) {
  LocalLockFreeMetric<int> metric;
  EXPECT_EQ(metric.Collect(), 0);
  metric.Update(1);
  EXPECT_EQ(metric.Collect(), 1);
  EXPECT_EQ(metric.Collect(), 0);
  // Another round.
  metric.Update(2);
  metric.Update(3);
  EXPECT_EQ(metric.Collect(), 5);
  EXPECT_EQ(metric.Collect(), 0);
}

TEST(LocalLockFreeMetricTest, ChangeSeenImmediately) {
  LocalLockFreeMetric<BackCollection<std::string>, char> metric;
  EXPECT_THAT(metric.Collect().collection, IsEmpty());
  metric.Update('a');
  EXPECT_THAT(metric.Collect().collection, Eq("a"));
  EXPECT_THAT(metric.Collect().collection, IsEmpty());
  // Another round.
  metric.Update('x');
  metric.Update('y');
  EXPECT_THAT(metric.Collect().collection, Eq("xy"));
  EXPECT_THAT(metric.Collect().collection, IsEmpty());
}

TEST(LocalLockFreeMetricTest, TwoThreads) {
  static constexpr int_least32_t kCount = 0x10000;
  LocalLockFreeMetric<BackCollection<std::deque<int_least32_t>>, int_least32_t>
      metric;
  absl::Notification updater_done;
  std::thread updater([&]() {
    absl::BitGen bitgen;
    for (int_least32_t i = 0; i < kCount; i++) {
      metric.Update(i);
      absl::SleepFor(absl::Nanoseconds(absl::Uniform(bitgen, 0, 1000)));
    }
    updater_done.Notify();
  });
  absl::BitGen bitgen;
  std::deque<int_least32_t> result;
  while (!updater_done.HasBeenNotified()) {
    AppendTo(metric.Collect().collection, result);
    absl::SleepFor(absl::Nanoseconds(absl::Uniform(bitgen, 0, 1000)));
  }
  updater.join();
  AppendTo(metric.Collect().collection, result);
  ASSERT_EQ(result.size(), kCount);
  for (int_least32_t i = 0; i < kCount; i++) {
    ASSERT_EQ(i, result[i]) << "at index " << i;
  }
}

TEST(LockFreeMetricTest, ChangeSeenInOnlyInTheRightMetric) {
  LockFreeMetric<int_least32_t> metric;
  LockFreeMetric<int_least32_t> other;
  EXPECT_THAT(metric.Collect(), IsEmpty());
  metric.Update(1);
  EXPECT_THAT(metric.Collect(), ElementsAre(1));
  EXPECT_THAT(metric.Collect(), ElementsAre(0));
  EXPECT_THAT(other.Collect(), IsEmpty())
      << "This thread hasn't created any thread-local local metrics for "
         "`other`";
  // Another round.
  metric.Update(2);
  metric.Update(3);
  EXPECT_THAT(metric.Collect(), ElementsAre(5));
  EXPECT_THAT(metric.Collect(), ElementsAre(0));
  EXPECT_THAT(other.Collect(), IsEmpty());
}

}  // namespace
}  // namespace simple_rcu
