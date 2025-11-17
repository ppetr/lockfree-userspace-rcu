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
struct LocalContext {
  static std::optional<LocalContext>& Static() {
    static std::optional<LocalContext> ctx;
    return ctx;
  }

  LocalLockFreeMetric<T> metric;
  T counter{};
};

template <typename T>
static void Setup(const benchmark::State&) {
  auto& context = LocalContext<T>::Static();
  ABSL_CHECK(!context.has_value()) << "LocalContext not teared down";
  context.emplace();
}

template <typename T>
static void Teardown(const benchmark::State& state) {
  auto& context = LocalContext<T>::Static();
  ABSL_CHECK(context.has_value()) << "Mismatched Teardown";
  context.reset();
}

static void BM_LocalTwoThreads(benchmark::State& state) {
  auto& ctx = *LocalContext<int_fast32_t>::Static();
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
BENCHMARK(BM_LocalTwoThreads)
    ->Threads(2)
    ->Range(1 << 10, 1 << 20)
    ->Setup(Setup<int_fast32_t>)
    ->Teardown(Teardown<int_fast32_t>);
// ->Complexity();

static void BM_MultiThreaded(benchmark::State& state) {
  static std::vector<int64_t> counts;
  static std::shared_ptr<LockFreeMetric<int_fast32_t>> metric;
  if (state.thread_index() == state.threads() - 1) {
    counts = std::vector<int64_t>(state.threads() - 1, 0);
    metric = LockFreeMetric<int_fast32_t>::New();
    int64_t measured = 0;
    for (auto _ : state) {
      for (int_fast32_t c : metric->Collect()) {
        measured += c;
      }
    }
    for (int_fast32_t c : metric->Collect()) {
      measured += c;
    }
    // Compare the results.
    int64_t expected = 0;
    for (int64_t i : counts) {
      expected += (i * (i + 1)) / 2;
    }
    // ABSL_CHECK_EQ(expected, measured);
  } else {
    const int i = state.thread_index();
    for (auto _ : state) {
      metric->Update(++counts[i]);
    }
  }
}
BENCHMARK(BM_MultiThreaded)->ThreadRange(2, 4)->Range(1 << 10, 1 << 20);
// ->Complexity();

}  // namespace
}  // namespace simple_rcu
