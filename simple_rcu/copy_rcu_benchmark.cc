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

#include "benchmark/benchmark.h"
#include "simple_rcu/copy_rcu.h"

namespace simple_rcu {
namespace {

struct ThreadBundle {
  ThreadBundle() : finished(false) {}
  ~ThreadBundle() {
    finished.store(true);
    for (auto& thread : threads) {
      thread.join();
    }
  }

  std::atomic<bool> finished;
  std::deque<std::thread> threads;
};

static void BM_Reads(benchmark::State& state) {
  static CopyRcu<int_fast32_t> rcu;
  ThreadBundle updater;
  if (state.thread_index() == 0) {
    for (int i = 0; i < state.range(0); i++) {
      updater.threads.emplace_back([&]() {
        int_fast32_t updates = 0;
        while (!updater.finished.load()) {
          benchmark::DoNotOptimize(rcu.Update(updates++));
        }
      });
    }
  }
  static thread_local CopyRcu<int_fast32_t>::Local reader(rcu);
  for (auto _ : state) {
    benchmark::DoNotOptimize(*reader.Read());
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_Reads)->ThreadRange(1, 3)->Arg(1)->Arg(4);

static void BM_ReadSharedPtrs(benchmark::State& state) {
  static Rcu<int_fast32_t> rcu(std::make_shared<int_fast32_t>(0));
  ThreadBundle updater;
  if (state.thread_index() == 0) {
    for (int i = 0; i < state.range(0); i++) {
      updater.threads.emplace_back([&]() {
        int_fast32_t updates = 0;
        while (!updater.finished.load()) {
          benchmark::DoNotOptimize(
              rcu.Update(std::make_shared<int_fast32_t>(updates++)));
        }
      });
    }
  }
  static thread_local Rcu<int_fast32_t>::Local reader(rcu);
  for (auto _ : state) {
    benchmark::DoNotOptimize(*reader.ReadPtr());
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_ReadSharedPtrs)->ThreadRange(1, 3)->Arg(1)->Arg(4);

static void BM_Updates(benchmark::State& state) {
  static CopyRcu<int_fast32_t> rcu;
  ThreadBundle reader;
  if (state.thread_index() == 0) {
    for (int i = 0; i < state.range(0); i++) {
      reader.threads.emplace_back([&]() {
        static thread_local CopyRcu<int_fast32_t>::Local local(rcu);
        while (!reader.finished.load()) {
          benchmark::DoNotOptimize(*local.Read());
        }
      });
    }
  }
  int_fast32_t updates = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(rcu.Update(++updates));
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_Updates)->ThreadRange(1, 3)->Arg(1)->Arg(4);

}  // namespace
}  // namespace simple_rcu
