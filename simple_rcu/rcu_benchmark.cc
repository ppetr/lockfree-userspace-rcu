// Copyright 2022 Google LLC
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
#include "simple_rcu/rcu.h"

namespace simple_rcu {
namespace {

static void BM_Reads(benchmark::State& state) {
  std::atomic<bool> finished(false);
  static Rcu<int_fast32_t> rcu;
  std::deque<std::thread> updater_threads;
  if (state.thread_index() == 0) {
    for (int i = 0; i < state.range(0); i++) {
      updater_threads.emplace_back([&]() {
        int_fast32_t updates = 0;
        while (!finished.load()) {
          benchmark::DoNotOptimize(rcu.Update(updates++));
        }
      });
    }
  }
  static thread_local Rcu<int_fast32_t>::Local reader(rcu);
  for (auto _ : state) {
    benchmark::DoNotOptimize(*reader.Read());
    benchmark::ClobberMemory();
  }
  finished.store(true);
  for (auto& thread : updater_threads) {
    thread.join();
  }
}
BENCHMARK(BM_Reads)->ThreadRange(1, 3)->Arg(1)->Arg(4);

static void BM_Updates(benchmark::State& state) {
  std::atomic<bool> finished(false);
  static Rcu<int_fast32_t> rcu;
  std::deque<std::thread> reader_threads;
  if (state.thread_index() == 0) {
    for (int i = 0; i < state.range(0); i++) {
      reader_threads.emplace_back([&]() {
        static thread_local Rcu<int_fast32_t>::Local reader(rcu);
        while (!finished.load()) {
          benchmark::DoNotOptimize(*reader.Read());
        }
      });
    }
  }
  int_fast32_t updates = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(rcu.Update(++updates));
    benchmark::ClobberMemory();
  }
  finished.store(true);
  for (auto& thread : reader_threads) {
    thread.join();
  }
}
BENCHMARK(BM_Updates)->ThreadRange(1, 3)->Arg(1)->Arg(4);

}  // namespace
}  // namespace simple_rcu
