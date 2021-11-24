# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.11)

enable_testing()

set(CMAKE_CXX_STANDARD 11)
# Set variables for subdirectories.
# See https://stackoverflow.com/a/3769269/1333025.
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL
    "Disable Benchmark library tests")
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "")

project(refcounted-var-sized CXX)

add_subdirectory(abseil-cpp)

add_subdirectory(googletest EXCLUDE_FROM_ALL)

# For the benchmarks below be sure to enable optimizations, for example:
#   cmake -DBENCHMARK_ENABLE_LTO=true -DCMAKE_BUILD_TYPE=Release
# See https://github.com/google/benchmark#debug-vs-release.
add_subdirectory(benchmark EXCLUDE_FROM_ALL)

# Var-sized and ref-counted structures.

add_library(ref INTERFACE)
target_include_directories(ref INTERFACE .)
target_link_libraries(ref INTERFACE absl::utility absl::variant)

add_executable(ref_test ref_test.cc)
target_link_libraries(ref_test absl::utility absl::variant gtest_main)
add_test(NAME ref_test COMMAND ref_test)

add_library(var_sized INTERFACE)
target_include_directories(var_sized INTERFACE .)
target_link_libraries(var_sized INTERFACE ref)

add_executable(var_sized_test var_sized_test.cc)
target_link_libraries(var_sized_test var_sized absl::strings gtest_main)
add_test(NAME var_sized_test COMMAND var_sized_test)

add_executable(var_sized_benchmark var_sized_benchmark.cc)
target_link_libraries(var_sized_benchmark var_sized absl::memory benchmark::benchmark_main)
add_test(NAME var_sized_benchmark COMMAND var_sized_benchmark)

# Copy on Write.

add_library(copy_on_write INTERFACE)
target_include_directories(copy_on_write INTERFACE .)
target_link_libraries(copy_on_write INTERFACE ref absl::utility)

add_executable(copy_on_write_test copy_on_write_test.cc)
target_link_libraries(copy_on_write_test copy_on_write absl::strings absl::utility gtest_main)
add_test(NAME copy_on_write_test COMMAND copy_on_write_test)
