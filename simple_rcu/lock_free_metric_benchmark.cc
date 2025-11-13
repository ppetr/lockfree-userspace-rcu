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

#include <cassert>
#include <cstdint>
#include <optional>

#include "absl/log/absl_check.h"
#include "benchmark/benchmark.h"
#include "simple_rcu/lock_free_metric.h"

namespace simple_rcu {
namespace {

template <typename T>
struct Context {
  static std::optional<Context>& Static() {
    static std::optional<Context> ctx;
    return ctx;
  }

  LocalLockFreeMetric<T> metric;
  T counter{};
};

template <typename T>
static void Setup(const benchmark::State&) {
  auto& context = Context<T>::Static();
  ABSL_CHECK(!context.has_value()) << "Context not teared down";
  context.emplace();
}

template <typename T>
static void Teardown(const benchmark::State& state) {
  auto& context = Context<T>::Static();
  ABSL_CHECK(context.has_value()) << "Mismatched Teardown";
  context.reset();
}

static void BM_TwoThreads(benchmark::State& state) {
  // A type that's not lock-free on any current architecture.
  auto& ctx = *Context<int_fast32_t>::Static();
  if (state.thread_index() == 0) {
    int64_t total = 0;
    for (auto _ : state) {
      total += ctx.metric.Collect();
    }
    total += ctx.metric.Collect();
    // Compare the results.
    int64_t expected = ctx.counter;
    expected = (expected * (expected + 1)) / 2;
    ABSL_CHECK_EQ(expected, total);
  } else {
    ABSL_CHECK_EQ(state.thread_index(), 1);
    for (auto _ : state) {
      ctx.metric.Update(++ctx.counter);
    }
  }
}
BENCHMARK(BM_TwoThreads)
    ->Threads(2)
    ->Range(1 << 10, 1 << 20)
    ->Setup(Setup<int_fast32_t>)
    ->Teardown(Teardown<int_fast32_t>)
    ->Complexity();

}  // namespace
}  // namespace simple_rcu
