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
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace simple_rcu {
namespace {

using ::testing::_;
using ::testing::Pair;

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
  using value_type = std::optional<typename C::value_type>;

  BackCollection() = default;
  BackCollection(BackCollection const&) = default;
  BackCollection(BackCollection&&) = default;
  BackCollection& operator=(BackCollection const&) = default;
  BackCollection& operator=(BackCollection&&) = default;

  BackCollection& operator+=(value_type element) {
    if (element.has_value()) {
      collection.push_back(*std::move(element));
    }
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

TEST(TwoThreadConcurrentTest, ChangeSeenImmediatelyString) {
  TwoThreadConcurrent<std::string> ttc("");
  EXPECT_THAT(ttc.Update<false>("a"), Pair("", _));
  EXPECT_THAT(ttc.Update<true>("b"), Pair("a", true));
  EXPECT_THAT(ttc.Update<true>("c"), Pair("ab", false));
  // Another round.
  EXPECT_THAT(ttc.Update<false>("x"), Pair("abc", _));
  EXPECT_THAT(ttc.Update<false>(""), Pair("abcx", false));
  EXPECT_THAT(ttc.Update<true>("y"), Pair("abcx", true));
  EXPECT_THAT(ttc.Update<true>(""), Pair("abcxy", false));
}

TEST(TwoThreadConcurrentTest, SelfVisibleChangesString) {
  TwoThreadConcurrent<std::string> ttc("");
  EXPECT_THAT(ttc.Update<false>("a"), Pair("", _));
  EXPECT_EQ(ttc.ObserveLast<false>(), "a");
  EXPECT_EQ(ttc.ObserveLast<false>(), "a");
  EXPECT_THAT(ttc.Update<true>("b"), Pair("a", _));
  EXPECT_EQ(ttc.ObserveLast<true>(), "ab");
  EXPECT_EQ(ttc.ObserveLast<true>(), "ab");
  EXPECT_THAT(ttc.Update<true>("c"), Pair("ab", _));
  EXPECT_EQ(ttc.ObserveLast<true>(), "abc");
  EXPECT_EQ(ttc.ObserveLast<true>(), "abc");
  // Another round.
  EXPECT_THAT(ttc.Update<false>("x"), Pair("abc", _));
  EXPECT_EQ(ttc.ObserveLast<false>(), "abcx");
  EXPECT_EQ(ttc.ObserveLast<false>(), "abcx");
  EXPECT_THAT(ttc.Update<false>(""), Pair("abcx", _));
  EXPECT_EQ(ttc.ObserveLast<false>(), "abcx");
  EXPECT_EQ(ttc.ObserveLast<false>(), "abcx");
  EXPECT_THAT(ttc.Update<true>("y"), Pair("abcx", _));
  EXPECT_EQ(ttc.ObserveLast<true>(), "abcxy");
  EXPECT_EQ(ttc.ObserveLast<true>(), "abcxy");
  EXPECT_THAT(ttc.Update<true>(""), Pair("abcxy", _));
  EXPECT_EQ(ttc.ObserveLast<true>(), "abcxy");
  EXPECT_EQ(ttc.ObserveLast<true>(), "abcxy");
}

TEST(TwoThreadConcurrentTest, ZigZag) {
  TwoThreadConcurrent<int> ttc;
  ASSERT_THAT(ttc.Update<false>(1), Pair(0, _));
  EXPECT_THAT(ttc.Update<true>(2), Pair(1, true));
  EXPECT_THAT(ttc.Update<false>(4), Pair(3, true));
  EXPECT_THAT(ttc.Update<true>(8), Pair(7, true));
  EXPECT_THAT(ttc.Update<false>(16), Pair(15, true));
  EXPECT_THAT(ttc.Update<true>(32), Pair(31, true));
  EXPECT_THAT(ttc.Update<true>(0), Pair(63, false));
}

TEST(TwoThreadConcurrentTest, Sequence) {
  using C = BackCollection<std::deque<int_least32_t>>;
  static constexpr int_least32_t kCount = 0x100;
  absl::BitGen bitgen;
  TwoThreadConcurrent<C, C::value_type> ttc;
  std::vector<bool> bits;
  for (int_least32_t i = 0; i < kCount; i++) {
    const C* last;
    const bool bit = bits.emplace_back(bool{absl::Bernoulli(bitgen, 0.5)});
    if (bit) {
      last = &ttc.Update<true>(i).first;
    } else {
      last = &ttc.Update<false>(i).first;
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
  using C = BackCollection<std::deque<int_least32_t>>;
  static constexpr int_least32_t kCount = 0x100;
  std::atomic<int_fast32_t> counter(0);
  TwoThreadConcurrent<C, C::value_type> ttc;
  absl::Notification updater_done;
  std::thread updater([&]() {
    absl::BitGen bitgen;
    for (int_least32_t i = 0; i < kCount; i++) {
      ttc.Update<false>(counter += 1);
      absl::SleepFor(absl::Nanoseconds(absl::Uniform(bitgen, 0, 1000)));
    }
    updater_done.Notify();
  });
  absl::BitGen bitgen;
  while (!updater_done.HasBeenNotified()) {
    ttc.Update<true>(counter += 1);
    absl::SleepFor(absl::Nanoseconds(absl::Uniform(bitgen, 0, 1000)));
  }
  updater.join();
  const auto& result = ttc.Update<true>(counter += 1).first;
  // Elements can be inserted out of order wrt `counter`.
  std::deque<int_least32_t> collection = result.collection;
  ASSERT_EQ(collection.size(), counter.load() - 1) << result;
  std::sort(collection.begin(), collection.end());
  for (int_least32_t i = 0; i < collection.size(); i++) {
    ASSERT_EQ(i + 1, collection[i]) << "at index " << i << "; " << result;
  }
}

TEST(TwoThreadConcurrentTest, NoDefaultConstructible) {
  struct Operation {};
  struct Foo {
    explicit Foo(std::in_place_t) {}

    Foo& operator+=(const Operation&) { return *this; }
  };

  TwoThreadConcurrent<Foo, Operation>(Foo(std::in_place))
      .Update<false>(Operation());
}

// Wraps `C` to be acted upon by an arbitrary functor using
// `operator+=`. This allows processing arbitrary changes on any
// copyable `C` objects with:
//
//   LockFreeMetric<AnyFunctor<C>, std::function<void(C&)>>
//
// See test `ArbitraryFunctor` below.
template <typename C>
struct AnyFunctor {
  using value_type = C;
  using diff_type = std::function<void(C&)>;

  static constexpr inline diff_type NoOp() { return {}; }

  static void Update(value_type& target, const diff_type& f) {
    if (f != nullptr) {
      f(target);
    }
  }
};

TEST(TwoThreadConcurrentTest, ArbitraryFunctor) {
  using StringF = AnyFunctor<std::string>;
  TwoThreadConcurrent<std::string, StringF::diff_type, StringF> ttc;
  ttc.Update<false>([](std::string& s) { s.append("abc"); });
  ttc.Update<false>([](std::string& s) { s.insert(0, "xyz-"); });
  EXPECT_EQ(ttc.Update<true>({}).first, "xyz-abc");
}

}  // namespace
}  // namespace simple_rcu
