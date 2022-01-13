// Copyright 2020 Google LLC
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

// Benchmarks comparing var-sized, reference-counted data structures to
// unique/shared pointers.
//
// Results ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Raspberry Pi 4, clang++-11, compiled with
//   -DBENCHMARK_ENABLE_LTO=true -DCMAKE_BUILD_TYPE=Release
//   -DCMAKE_TOOLCHAIN_FILE=clang-toolchain.make
// Run on (4 X 1500 MHz CPU s)
// ------------------------------------------------------------------
// Benchmark                        Time             CPU   Iterations
// ------------------------------------------------------------------
// BM_VarSizedUniqueString       7964 ns         7948 ns        88029
// BM_VarSizedSharedString      11800 ns        11773 ns        59040
// BM_MakeUniqueStdString       <N/A - optimized away>
// BM_SharedStdString           29398 ns        29340 ns        24350
// BM_MakeSharedStdString       19331 ns        19292 ns        35853
//
// Raspberry Pi 4, g++-8.3.0, compiled with
//   -DBENCHMARK_ENABLE_LTO=true -DCMAKE_BUILD_TYPE=Release
// Run on (4 X 1500 MHz CPU s)
// ------------------------------------------------------------------
// Benchmark                        Time             CPU   Iterations
// ------------------------------------------------------------------
// BM_VarSizedUniqueString       7432 ns         7396 ns        94677
// BM_VarSizedSharedString      11417 ns        11373 ns        61362
// BM_MakeUniqueStdString       16206 ns        16141 ns        42563
// BM_SharedStdString           27904 ns        27796 ns        25215
// BM_MakeSharedStdString       19107 ns        18997 ns        36993

#include "var_sized.h"

#include <cassert>
#include <cstring>
#include <memory>

#include "absl/memory/memory.h"
#include "benchmark/benchmark.h"

namespace {

// Copies `len` elements from `text` to an array provided by var-sized
// allocated array at `buf`.
class VarSizedString {
 public:
  void SetArray(char* array, size_t length) {
    strncpy(array_ = array, "Lorem ipsum dolor sit amet", length);
  }

 private:
  char* array_;
};

}  // namespace

static void BM_VarSizedUniqueString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      char* array;
      auto unique = refptr::MakeUnique<VarSizedString, char>(16, array);
      unique->SetArray(array, 16);
    }
  }
}
BENCHMARK(BM_VarSizedUniqueString);

static void BM_VarSizedSharedString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      char* array;
      auto shared = refptr::MakeShared<VarSizedString, char>(16, array);
      shared->SetArray(array, 16);
    }
  }
}
BENCHMARK(BM_VarSizedSharedString);

static void BM_MakeUniqueStdString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      auto unique = absl::make_unique<VarSizedString>();
      std::unique_ptr<char[]> array(new char[16]);
      unique->SetArray(array.get(), 16);
    }
  }
}
BENCHMARK(BM_MakeUniqueStdString);

static void BM_SharedStdString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      // std::make_shared avoids the secondary heap allocation for its control
      // block.
      auto shared = std::shared_ptr<VarSizedString>(new VarSizedString());
      std::unique_ptr<char[]> array(new char[16]);
      shared->SetArray(array.get(), 16);
    }
  }
}
BENCHMARK(BM_SharedStdString);

// Using `make_shared` allocates both the value and the reference counter in a
// single memory block, thus being a bit more efficient.
static void BM_MakeSharedStdString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      auto shared = std::make_shared<VarSizedString>();
      std::unique_ptr<char[]> array(new char[16]);
      shared->SetArray(array.get(), 16);
    }
  }
}
BENCHMARK(BM_MakeSharedStdString);
