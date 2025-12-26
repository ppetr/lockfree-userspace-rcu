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
  ABSL_CHECK(!context.has_value()) << "Context not teared down";
  context.emplace();
}

template <typename C>
static void Teardown(const benchmark::State& state) {
  auto& context = C::Static();
  ABSL_CHECK(context.has_value()) << "Mismatched Teardown";
  context.reset();
}

template <typename T>
struct Context {
  static std::optional<Context>& Static() {
    static std::optional<Context> ctx;
    return ctx;
  }

  TwoThreadConcurrent<T> ttc;
  T counter{};
};

static void BM_TwoThreads(benchmark::State& state) {
  auto& ctx = *Context<int_fast64_t>::Static();
  if (state.thread_index() == 0) {
    int_fast64_t counter = 0;
    for (auto _ : state) {
      ctx.ttc.Update<false>(++counter);
    }
    // Compare the results.
    const int_fast64_t total = ctx.ttc.Update<false>(0).first;
    ABSL_CHECK_EQ(
        (ctx.counter * (ctx.counter + 1) + counter * (counter + 1)) / 2, total)
        << "counter=" << counter << ", ctx.counter=" << ctx.counter;
  } else {
    for (auto _ : state) {
      ctx.ttc.Update<true>(++ctx.counter);
    }
  }
}
BENCHMARK(BM_TwoThreads)
    ->Threads(1)
    ->Threads(2)
    ->Setup(Setup<Context<int_fast64_t>>)
    ->Teardown(Teardown<Context<int_fast64_t>>)
    ->Complexity();

}  // namespace
}  // namespace simple_rcu
