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

#include <cstdint>
#include <memory>

#include "absl/base/attributes.h"
#include "absl/log/absl_check.h"
#include "benchmark/benchmark.h"
#include "simple_rcu/thread_local.h"

namespace simple_rcu {
namespace {

/* TODO
static void BM_ThreadLocalForTrivialType(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(i += ThreadLocal<int, char>::Map().size());
  }
  benchmark::DoNotOptimize(i);
}
BENCHMARK(BM_ThreadLocalForTrivialType)->ThreadRange(1, 64)->Complexity();
*/

static void BM_MultiThreaded(benchmark::State& state) {
  static ThreadLocal<int, char> shared{-1};
  const int i = state.thread_index();
  if (i == 0) {
    shared = ThreadLocal<int, char>{0};
  }
  for (auto _ : state) {
    shared.try_emplace(0).first++;
  }
  benchmark::DoNotOptimize(shared.try_emplace().first);
}
BENCHMARK(BM_MultiThreaded)->ThreadRange(1, 64)->Complexity();

}  // namespace
}  // namespace simple_rcu
