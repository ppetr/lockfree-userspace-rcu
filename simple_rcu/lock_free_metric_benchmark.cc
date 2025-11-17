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
#include <memory>
#include <optional>
#include <vector>

#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "benchmark/benchmark.h"
#include "simple_rcu/lock_free_metric.h"

namespace simple_rcu {
namespace {

template <typename C>
static void Setup(const benchmark::State&) {
  auto& context = C::Static();
  ABSL_CHECK(!context.has_value()) << "LocalContext not teared down";
  context.emplace();
}

template <typename C>
static void Teardown(const benchmark::State& state) {
  auto& context = C::Static();
  ABSL_CHECK(context.has_value()) << "Mismatched Teardown";
  context.reset();
}

template <typename T>
struct LocalContext {
  static std::optional<LocalContext>& Static() {
    static std::optional<LocalContext> ctx;
    return ctx;
  }

  LocalLockFreeMetric<T> metric;
  T counter{};
};

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
    ->Setup(Setup<LocalContext<int_fast32_t>>)
    ->Teardown(Teardown<LocalContext<int_fast32_t>>)
    ->Complexity();

template <typename T>
struct Context {
  static std::optional<Context>& Static() {
    static std::optional<Context> ctx;
    return ctx;
  }

  std::vector<int64_t> counts;
  std::shared_ptr<LockFreeMetric<T>> metric =
      LockFreeMetric<int_fast64_t>::New();
};

static void BM_MultiThreadedUpdate(benchmark::State& state) {
  auto& ctx = *Context<int_fast64_t>::Static();
  const int i = state.thread_index();
  if (i == 0) {
    ctx.counts = std::vector<int64_t>(state.threads(), 0);
  }
  auto local = ctx.metric->ThreadLocalView();
  for (auto _ : state) {
    local->Update(++ctx.counts[i]);
  }
  if (i == 0) {
    int64_t measured = 0;
    for (int_fast64_t c : ctx.metric->Collect()) {
      measured += c;
    }
    // Compare the results.
    int64_t expected = 0;
    for (int64_t c : ctx.counts) {
      expected += (c * (c + 1)) / 2;
    }
    ABSL_CHECK_EQ(expected, measured);
  }
}
BENCHMARK(BM_MultiThreadedUpdate)
    ->ThreadRange(1, 64)
    ->Setup(Setup<Context<int_fast64_t>>)
    ->Teardown(Teardown<Context<int_fast64_t>>)
    ->Complexity();

static void BM_MultiThreadedCollect(benchmark::State& state) {
  auto& ctx = *Context<int_fast64_t>::Static();
  const int i = state.thread_index();
  if (i == 0) {
    ctx.counts = std::vector<int64_t>(state.threads(), 0);
    int64_t measured = 0;
    for (auto _ : state) {
      for (int_fast64_t c : ctx.metric->Collect()) {
        measured += c;
      }
    }
    for (int_fast64_t c : ctx.metric->Collect()) {
      measured += c;
    }
    // Compare the results.
    int64_t expected = 0;
    for (int64_t c : ctx.counts) {
      expected += (c * (c + 1)) / 2;
    }
    ABSL_CHECK_EQ(expected, measured);
  } else {
    auto local = ctx.metric->ThreadLocalView();
    for (auto _ : state) {
      local->Update(++ctx.counts[i]);
    }
  }
}
BENCHMARK(BM_MultiThreadedCollect)
    ->ThreadRange(1, 64)
    ->Setup(Setup<Context<int_fast64_t>>)
    ->Teardown(Teardown<Context<int_fast64_t>>)
    ->Complexity();

}  // namespace
}  // namespace simple_rcu
