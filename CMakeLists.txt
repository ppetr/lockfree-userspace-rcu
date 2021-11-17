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

cmake_minimum_required(VERSION 3.5)

enable_testing()

project(refcounted-var-sized CXX)

set(CMAKE_CXX_STANDARD 11)

add_subdirectory(abseil-cpp)

# TODO: Add other interfaces in the library.

add_library(copy_on_write INTERFACE)
target_include_directories(copy_on_write INTERFACE .)
target_link_libraries(copy_on_write INTERFACE absl::utility)

add_executable(copy_on_write_test copy_on_write_test.cc)
target_link_libraries(copy_on_write_test copy_on_write absl::utility)
add_test(NAME copy_on_write_test COMMAND copy_on_write_test)
