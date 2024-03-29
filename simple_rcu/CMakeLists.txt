# Copyright 2022-2023 Google LLC
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

add_library(local_3state_rcu INTERFACE)
target_include_directories(local_3state_rcu INTERFACE .)
#target_link_libraries(local_3state_rcu INTERFACE absl::...)

add_executable(local_3state_rcu_test local_3state_rcu_test.cc)
target_link_libraries(local_3state_rcu_test gtest_main)
add_test(NAME local_3state_rcu_test COMMAND local_3state_rcu_test)

add_library(thread_local INTERFACE)
target_include_directories(thread_local INTERFACE .)
target_link_libraries(thread_local INTERFACE absl::absl_check absl::core_headers)

add_library(copy_rcu INTERFACE)
target_include_directories(copy_rcu INTERFACE .)
target_link_libraries(copy_rcu INTERFACE local_3state_rcu thread_local absl::core_headers absl::flat_hash_map absl::flat_hash_set absl::function_ref absl::absl_log absl::memory absl::optional absl::synchronization absl::variant atomic)

add_executable(copy_rcu_test copy_rcu_test.cc)
target_link_libraries(copy_rcu_test copy_rcu gmock gtest_main)
add_test(NAME copy_rcu_test COMMAND copy_rcu_test)

add_executable(copy_rcu_benchmark copy_rcu_benchmark.cc)
target_link_libraries(copy_rcu_benchmark copy_rcu absl::absl_check absl::optional benchmark::benchmark_main)
add_test(NAME copy_rcu_benchmark COMMAND copy_rcu_benchmark)



add_library(reverse_rcu INTERFACE)
target_include_directories(reverse_rcu INTERFACE .)
target_link_libraries(reverse_rcu INTERFACE local_3state_rcu absl::core_headers absl::flat_hash_set absl::synchronization absl::utility atomic)

add_executable(reverse_rcu_test reverse_rcu_test.cc)
target_link_libraries(reverse_rcu_test reverse_rcu gtest_main)
add_test(NAME reverse_rcu_test COMMAND reverse_rcu_test)
