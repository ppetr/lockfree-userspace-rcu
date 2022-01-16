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

#include <cassert>
#include <cstring>
#include <memory>

#include "absl/memory/memory.h"
#include "benchmark/benchmark.h"
#include "var_sized.h"

namespace {

// Copies `len` elements from `text` to an array provided by var-sized
// allocated array at `buf`.
class VarSizedString {
 public:
  const char* SetArray(char* array, size_t length) {
    return strncpy(array_ = array, "Lorem ipsum dolor sit amet", length);
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
      benchmark::DoNotOptimize(unique->SetArray(array, 16));
      benchmark::ClobberMemory();
    }
  }
}
BENCHMARK(BM_VarSizedUniqueString);

static void BM_VarSizedSharedString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      char* array;
      auto shared = refptr::MakeShared<VarSizedString, char>(16, array);
      benchmark::DoNotOptimize(shared->SetArray(array, 16));
      benchmark::ClobberMemory();
    }
  }
}
BENCHMARK(BM_VarSizedSharedString);

static void BM_VarSizedRefCountedString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      char* array;
      auto ref = refptr::MakeRefCounted<VarSizedString, char>(16, array);
      benchmark::DoNotOptimize(ref->SetArray(array, 16));
      benchmark::ClobberMemory();
    }
  }
}
BENCHMARK(BM_VarSizedRefCountedString);

static void BM_VarSizedRefCountedSharedString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      char* array;
      auto ref = refptr::MakeRefCounted<VarSizedString, char>(16, array);
      // Move the value to a shared pointer and back to trigger its atomic
      // refcount operations.
      auto shared = std::move(ref).Share();
      benchmark::DoNotOptimize(absl::get<0>(std::move(shared).AttemptToClaim())
                                   ->SetArray(array, 16));
      benchmark::ClobberMemory();
    }
  }
}
BENCHMARK(BM_VarSizedRefCountedSharedString);

static void BM_MakeUniqueStdString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      auto unique = absl::make_unique<VarSizedString>();
      std::unique_ptr<char[]> array(new char[16]);
      benchmark::DoNotOptimize(unique->SetArray(array.get(), 16));
      benchmark::ClobberMemory();
    }
  }
}
BENCHMARK(BM_MakeUniqueStdString);

static void BM_SharedStdString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      auto shared = std::shared_ptr<VarSizedString>(new VarSizedString());
      std::unique_ptr<char[]> array(new char[16]);
      benchmark::DoNotOptimize(shared->SetArray(array.get(), 16));
      benchmark::ClobberMemory();
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
      benchmark::DoNotOptimize(shared->SetArray(array.get(), 16));
      benchmark::ClobberMemory();
    }
  }
}
BENCHMARK(BM_MakeSharedStdString);
