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

#include "var_sized.h"

#include <cassert>
#include <cstring>
#include <memory>

#include <benchmark/benchmark.h>

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

static void BM_AllocatedString(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; i++) {
      (void)std::unique_ptr<AllocatedString>(
          new AllocatedString(16, "Lorem ipsum dolor sit amet"));
    }
  }
}
BENCHMARK(BM_AllocatedString);
