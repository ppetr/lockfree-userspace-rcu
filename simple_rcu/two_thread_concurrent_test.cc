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

#include "simple_rcu/two_thread_concurrent.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <thread>

#include "absl/random/random.h"
#include "absl/strings/str_format.h"
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

template <typename C, typename Sink>
void FormatCollection(Sink& sink, const C& c) {
  sink.Append("[");
  auto it = c.begin();
  if (it != c.end()) {
    while (true) {
      absl::Format(&sink, "%v", *it);
      if (++it == c.end()) {
        break;
      } else {
        sink.Append(",");
      }
    }
  }
  sink.Append("]");
}

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

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const BackCollection& c) {
    FormatCollection(sink, c.collection);
  }

  friend std::ostream& operator<<(std::ostream& os, const BackCollection& c) {
    return os << absl::StreamFormat("%v", c);
  }

  C collection;
};

template <typename T>
void AppendTo(std::deque<T>&& input, std::deque<T>& target) {
  target.insert(target.end(), std::make_move_iterator(input.begin()),
                std::make_move_iterator(input.end()));
}

TEST(TwoThreadConcurrentTest, ChangeSeenImmediatelyInt) {
  TwoThreadConcurrent<int> metric;
  /*
  EXPECT_EQ(metric.Update<true>(0), 0);
  metric.Update<false>(0);
  metric.Update<false>(0);
  */

  metric.Update<false>(1);
  EXPECT_EQ(metric.Update<true>(2), 1);
  EXPECT_EQ(metric.Update<true>(0), 3);
  // Another round.
  EXPECT_EQ(metric.Update<false>(2), 3);
  EXPECT_EQ(metric.Update<false>(3), 5);
  EXPECT_EQ(metric.Update<true>(0), 8);
  EXPECT_EQ(metric.Update<true>(0), 8);
}

TEST(TwoThreadConcurrentTest, ZigZag) {
  TwoThreadConcurrent<int> metric;
  ASSERT_EQ(metric.Update<false>(1), 0);
  EXPECT_EQ(metric.Update<true>(2), 1);
  EXPECT_EQ(metric.Update<false>(4), 3);
  EXPECT_EQ(metric.Update<true>(8), 7);
  EXPECT_EQ(metric.Update<false>(16), 15);
  EXPECT_EQ(metric.Update<true>(32), 31);
  EXPECT_EQ(metric.Update<true>(0), 63);
}

/*
TEST(TwoThreadConcurrentTest, ChangeSeenImmediately) {
  TwoThreadConcurrent<BackCollection<std::string>, char> metric;
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
*/

TEST(TwoThreadConcurrentTest, Sequence) {
  static constexpr int_least32_t kCount = 0x100;
  absl::BitGen bitgen;
  TwoThreadConcurrent<BackCollection<std::deque<int_least32_t>>, int_least32_t>
      metric;
  std::vector<bool> bits;
  for (int_least32_t i = 0; i < kCount; i++) {
    const BackCollection<std::deque<int_least32_t>>* last;
    const bool bit = bits.emplace_back(bool{absl::Bernoulli(bitgen, 0.5)});
    if (bit) {
      last = &metric.Update<true>(i);
    } else {
      last = &metric.Update<false>(i);
    }
    ASSERT_EQ(last->collection.size(), i)
        << BackCollection<std::vector<bool>>{bits};
    if (i >= 1) {
      ASSERT_EQ(i - 1, last->collection.back())
          << "at index " << i << "; "
          << BackCollection<std::vector<bool>>{bits};
    }
  }
}

TEST(TwoThreadConcurrentTest, TwoThreads) {
  static constexpr int_least32_t kCount = 0x100;
  std::atomic<int_fast32_t> counter(0);
  TwoThreadConcurrent<BackCollection<std::deque<int_least32_t>>, int_least32_t>
      metric;
  absl::Notification updater_done;
  std::thread updater([&]() {
    absl::BitGen bitgen;
    for (int_least32_t i = 0; i < kCount; i++) {
      metric.Update<false>(counter += 1);
      absl::SleepFor(absl::Nanoseconds(absl::Uniform(bitgen, 0, 1000)));
    }
    updater_done.Notify();
  });
  absl::BitGen bitgen;
  while (!updater_done.HasBeenNotified()) {
    metric.Update<true>(counter += 1);
    absl::SleepFor(absl::Nanoseconds(absl::Uniform(bitgen, 0, 1000)));
  }
  updater.join();
  auto& result = metric.Update<true>(counter += 1);
  // Elements can be inserted out of order wrt `counter`.
  ABSL_LOG(INFO) << "Collected elements: " << result.collection.size();
  std::deque<int_least32_t> collection = result.collection;
  ASSERT_EQ(collection.size(), counter.load() - 1) << result;
  std::sort(collection.begin(), collection.end());
  for (int_least32_t i = 0; i < collection.size(); i++) {
    ASSERT_EQ(i + 1, collection[i]) << "at index " << i << "; " << result;
  }
}

}  // namespace
}  // namespace simple_rcu
