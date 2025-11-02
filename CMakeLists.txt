# Copyright 2022 Google LLC
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

set(CMAKE_CXX_STANDARD 17)
# Set variables for subdirectories.
# See https://stackoverflow.com/a/3769269/1333025.
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL
    "Disable Benchmark library tests")
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "")

project(simple-rcu CXX)

add_subdirectory(abseil-cpp)

add_subdirectory(googletest EXCLUDE_FROM_ALL)

# For the benchmarks below be sure to enable optimizations, for example:
#   cmake -DBENCHMARK_ENABLE_LTO=true -DCMAKE_BUILD_TYPE=Release
# See https://github.com/google/benchmark#debug-vs-release.
add_subdirectory(benchmark EXCLUDE_FROM_ALL)

include_directories(${PROJECT_SOURCE_DIR})
add_subdirectory(simple_rcu)
