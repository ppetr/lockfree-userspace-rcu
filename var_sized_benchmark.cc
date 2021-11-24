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
// Raspberry Pi 4, g++, compiled with ................................
//   -DBENCHMARK_ENABLE_LTO=true -DCMAKE_BUILD_TYPE=Release)
//
// Run on (4 X 1500 MHz CPU s)
// Load Average: 0.17, 0.05, 0.01
// -----------------------------------------------------------------------
// Benchmark                             Time             CPU   Iterations
// -----------------------------------------------------------------------
// BM_VarSizedString                  7176 ns         7159 ns        97705
// BM_UniqueAllocatedString          14833 ns        14800 ns        44468
// BM_SharedNewAllocatedString       24308 ns        24257 ns        28854
// BM_MakeSharedAllocatedString      17881 ns        17842 ns        39201
//
// Intel Core i5-3470 @ 3.20GHz, compiled with .......................
//   -DBENCHMARK_ENABLE_LTO=true -DCMAKE_BUILD_TYPE=Release)
//
// Run on (4 X 3600 MHz CPU s)
// CPU Caches:
//   L1 Data 32 KiB (x4)
//   L1 Instruction 32 KiB (x4)
//   L2 Unified 256 KiB (x4)
//   L3 Unified 6144 KiB (x1)
// Load Average: 3.60, 2.72, 1.67
// -----------------------------------------------------------------------
// Benchmark                             Time             CPU   Iterations
// -----------------------------------------------------------------------
// BM_VarSizedString                  1546 ns         1546 ns       451920
// BM_MakeUniqueAllocatedString       3058 ns         3058 ns       228779
// BM_SharedNewAllocatedString        5014 ns         5014 ns       138405
// BM_MakeSharedAllocatedString       3834 ns         3834 ns       182333

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
  VarSizedString(char* buf, size_t len, const char* text) : buf_(buf) {
    strncpy(buf, text, len);
    buf[len - 1] = '\0';
  }

  const char* text() const { return buf_; }

 private:
  const char* buf_;
};

// Allocates a `char` array of size `len` and copies `text` into it.
// Deletes the array on destruction.
class AllocatedString {
 public:
  AllocatedString(size_t len, const char* text) : buf_(new char[len]) {
    strncpy(buf_.get(), text, len);
    buf_.get()[len - 1] = '\0';
  }

  const char* text() const { return buf_.get(); }

 private:
  const std::unique_ptr<char[]> buf_;
};

}  // namespace

static void BM_VarSizedString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      (void)refptr::MakeUnique<VarSizedString, char>(
          16, "Lorem ipsum dolor sit amet");
    }
  }
}
BENCHMARK(BM_VarSizedString);

static void BM_MakeUniqueAllocatedString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      (void)absl::make_unique<AllocatedString>(16,
                                               "Lorem ipsum dolor sit amet");
    }
  }
}
BENCHMARK(BM_MakeUniqueAllocatedString);

// Creating a new `shared_ptr` involves one more heap allocation for its
// internal reference counter.
static void BM_SharedNewAllocatedString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      (void)std::shared_ptr<AllocatedString>(
          new AllocatedString(16, "Lorem ipsum dolor sit amet"));
    }
  }
}
BENCHMARK(BM_SharedNewAllocatedString);

// Using `make_shared` allocates both the value and the reference counter in a
// single memory block, thus being a bit more efficient.
static void BM_MakeSharedAllocatedString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      (void)std::make_shared<AllocatedString>(16, "Lorem ipsum dolor sit amet");
    }
  }
}
BENCHMARK(BM_MakeSharedAllocatedString);
