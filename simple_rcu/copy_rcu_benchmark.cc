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

#include <atomic>
#include <deque>
#include <thread>

#include "absl/log/absl_check.h"
#include "absl/types/optional.h"
#include "benchmark/benchmark.h"
#include "simple_rcu/copy_rcu.h"

namespace simple_rcu {
namespace {

template <typename T>
struct Context {
  Context(T initial_value) : rcu(std::move(initial_value)), finished(false) {}
  ~Context() {
    finished.store(true);
    for (auto& thread : threads) {
      thread.join();
    }
  }

  CopyRcu<T> rcu;
  std::atomic<bool> finished;
  std::deque<std::thread> threads;
};

template <typename T>
static absl::optional<Context<T>>& StaticContext() {
  static absl::optional<Context<T>> rcu;
  return rcu;
}

template <typename T>
static void Setup(T initial_value) {
  auto& context = StaticContext<T>();
  ABSL_CHECK(!context.has_value()) << "Context not teared down";
  context.emplace(std::move(initial_value));
}

template <typename T>
static void Teardown(const benchmark::State& state) {
  auto& context = StaticContext<T>();
  ABSL_CHECK(context.has_value()) << "Mismatched Teardown";
  context.reset();
}

static void BM_Reads(benchmark::State& state) {
  static auto& context = StaticContext<int_fast32_t>();
  if (state.thread_index() == 0) {
    for (int i = 0; i < state.range(0); i++) {
      context->threads.emplace_back([&]() {
        int_fast32_t updates = 0;
        while (!context->finished.load()) {
          benchmark::DoNotOptimize(context->rcu.Update(updates++));
        }
      });
    }
  }
  CopyRcu<int_fast32_t>::View& reader = context->rcu.ThreadLocalView();
  for (auto _ : state) {
    benchmark::DoNotOptimize(*reader.Read());
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_Reads)
    ->ThreadRange(1, 64)
    ->Arg(1)
    ->Arg(4)
    ->Setup([](const benchmark::State&) { Setup<int_fast32_t>(0); })
    ->Teardown(Teardown<int_fast32_t>);

static void BM_ReadsThreadLocal(benchmark::State& state) {
  static auto& context = StaticContext<int_fast32_t>();
  if (state.thread_index() == 0) {
    for (int i = 0; i < state.range(0); i++) {
      context->threads.emplace_back([&]() {
        int_fast32_t updates = 0;
        while (!context->finished.load()) {
          benchmark::DoNotOptimize(context->rcu.Update(updates++));
        }
      });
    }
  }
  for (auto _ : state) {
    benchmark::DoNotOptimize(context->rcu.Read());
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_ReadsThreadLocal)
    ->ThreadRange(1, 64)
    ->Arg(1)
    ->Arg(4)
    ->Setup([](const benchmark::State&) { Setup<int_fast32_t>(0); })
    ->Teardown(Teardown<int_fast32_t>);

static void BM_ReadSharedPtrs(benchmark::State& state) {
  static auto& context = StaticContext<std::shared_ptr<const int_fast32_t>>();
  if (state.thread_index() == 0) {
    for (int i = 0; i < state.range(0); i++) {
      context->threads.emplace_back([&]() {
        int_fast32_t updates = 0;
        while (!context->finished.load()) {
          benchmark::DoNotOptimize(*context->rcu.Update(
              std::make_shared<const int_fast32_t>(updates++)));
        }
      });
    }
  }
  Rcu<int_fast32_t>::View& local = context->rcu.ThreadLocalView();
  for (auto _ : state) {
    benchmark::DoNotOptimize(*local.ReadPtr());
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_ReadSharedPtrs)
    ->ThreadRange(1, 64)
    ->Arg(1)
    ->Arg(4)
    ->Setup([](const benchmark::State&) {
      Setup(std::make_shared<const int_fast32_t>(0));
    })
    ->Teardown(Teardown<std::shared_ptr<const int_fast32_t>>);

static void BM_ReadSharedPtrsThreadLocal(benchmark::State& state) {
  static auto& context = StaticContext<std::shared_ptr<const int_fast32_t>>();
  if (state.thread_index() == 0) {
    for (int i = 0; i < state.range(0); i++) {
      context->threads.emplace_back([&]() {
        int_fast32_t updates = 0;
        while (!context->finished.load()) {
          benchmark::DoNotOptimize(*context->rcu.Update(
              std::make_shared<const int_fast32_t>(updates++)));
        }
      });
    }
  }
  for (auto _ : state) {
    benchmark::DoNotOptimize(*context->rcu.ReadPtr());
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_ReadSharedPtrsThreadLocal)
    ->ThreadRange(1, 64)
    ->Arg(1)
    ->Arg(4)
    ->Setup([](const benchmark::State&) {
      Setup(std::make_shared<const int_fast32_t>(0));
    })
    ->Teardown(Teardown<std::shared_ptr<const int_fast32_t>>);

static void BM_Updates(benchmark::State& state) {
  static auto& context = StaticContext<int_fast32_t>();
  if (state.thread_index() == 0) {
    for (int i = 0; i < state.range(0); i++) {
      context->threads.emplace_back([&]() {
        CopyRcu<int_fast32_t>::View& local = context->rcu.ThreadLocalView();
        while (!context->finished.load()) {
          benchmark::DoNotOptimize(*local.Read());
        }
      });
    }
  }
  int_fast32_t updates = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(context->rcu.Update(++updates));
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_Updates)
    ->ThreadRange(1, 64)
    ->Arg(1)
    ->Arg(4)
    ->Setup([](const benchmark::State&) { Setup<int_fast32_t>(0); })
    ->Teardown(Teardown<int_fast32_t>);

}  // namespace
}  // namespace simple_rcu
