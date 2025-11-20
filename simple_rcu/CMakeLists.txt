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

add_library(lock_free_int INTERFACE)
target_include_directories(lock_free_int INTERFACE .)
target_link_libraries(lock_free_int INTERFACE atomic)

add_library(local_3state_rcu INTERFACE)
target_include_directories(local_3state_rcu INTERFACE .)
target_link_libraries(local_3state_rcu INTERFACE lock_free_int atomic)

add_executable(local_3state_rcu_test local_3state_rcu_test.cc)
target_link_libraries(local_3state_rcu_test gtest_main)
add_test(NAME local_3state_rcu_test COMMAND local_3state_rcu_test)

add_library(local_3state_exchange INTERFACE)
target_include_directories(local_3state_exchange INTERFACE .)
target_link_libraries(local_3state_exchange INTERFACE lock_free_int atomic)

add_library(thread_local thread_local.cc)
target_link_libraries(thread_local lock_free_int absl::absl_check absl::absl_log absl::die_if_null absl::flat_hash_map absl::core_headers)

add_executable(thread_local_benchmark thread_local_benchmark.cc)
target_link_libraries(thread_local_benchmark thread_local absl::absl_check benchmark::benchmark_main)
add_test(NAME thread_local_benchmark COMMAND thread_local_benchmark)

add_library(copy_rcu INTERFACE)
target_include_directories(copy_rcu INTERFACE .)
target_link_libraries(copy_rcu INTERFACE local_3state_rcu lock_free_int thread_local absl::absl_log absl::base absl::flat_hash_set absl::function_ref absl::optional absl::synchronization atomic)

add_executable(copy_rcu_test copy_rcu_test.cc)
target_link_libraries(copy_rcu_test copy_rcu gmock gtest_main)
add_test(NAME copy_rcu_test COMMAND copy_rcu_test)

add_executable(copy_rcu_benchmark copy_rcu_benchmark.cc)
target_link_libraries(copy_rcu_benchmark copy_rcu benchmark::benchmark_main)
add_test(NAME copy_rcu_benchmark COMMAND copy_rcu_benchmark)

add_library(lock_free_metric INTERFACE)
target_include_directories(lock_free_metric INTERFACE .)
target_link_libraries(lock_free_metric INTERFACE local_3state_exchange thread_local absl::base absl::absl_check absl::absl_log)

add_executable(lock_free_metric_benchmark lock_free_metric_benchmark.cc)
target_link_libraries(lock_free_metric_benchmark lock_free_metric absl::absl_check absl::absl_log benchmark::benchmark_main)
add_test(NAME lock_free_metric_benchmark COMMAND lock_free_metric_benchmark)

add_executable(lock_free_metric_test lock_free_metric_test.cc)
target_link_libraries(lock_free_metric_test lock_free_metric absl::random_random absl::synchronization absl::time gmock gtest_main)
add_test(NAME lock_free_metric_test COMMAND lock_free_metric_test)



add_library(reverse_rcu INTERFACE)
target_include_directories(reverse_rcu INTERFACE .)
target_link_libraries(reverse_rcu INTERFACE local_3state_rcu absl::base absl::flat_hash_set absl::synchronization absl::utility atomic)

add_executable(reverse_rcu_test reverse_rcu_test.cc)
target_link_libraries(reverse_rcu_test reverse_rcu gtest_main)
add_test(NAME reverse_rcu_test COMMAND reverse_rcu_test)
