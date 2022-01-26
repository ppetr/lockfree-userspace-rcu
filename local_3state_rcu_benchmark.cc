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

#include "benchmark/benchmark.h"
#include "local_3state_rcu.h"

namespace simple_rcu {
namespace {

static void BM_UpdateAndReadSingleThreaded(benchmark::State& state) {
  Local3StateRcu<int_fast32_t> rcu;
  for (auto _ : state) {
    benchmark::DoNotOptimize(rcu.Update() = state.iterations());
    rcu.TriggerUpdate();
    rcu.TriggerRead();
    benchmark::DoNotOptimize(rcu.Read());
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_UpdateAndReadSingleThreaded);

}  // namespace
}  // namespace simple_rcu
