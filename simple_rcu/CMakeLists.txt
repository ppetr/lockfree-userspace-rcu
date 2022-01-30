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

add_library(local_3state_rcu INTERFACE)
target_include_directories(local_3state_rcu INTERFACE .)
#target_link_libraries(local_3state_rcu INTERFACE absl::...)

add_executable(local_3state_rcu_test local_3state_rcu_test.cc)
target_link_libraries(local_3state_rcu_test gtest_main)
add_test(NAME local_3state_rcu_test COMMAND local_3state_rcu_test)

add_library(rcu INTERFACE)
target_include_directories(rcu INTERFACE .)
target_link_libraries(rcu INTERFACE local_3state_rcu absl::flat_hash_set absl::synchronization atomic)

add_executable(rcu_test rcu_test.cc)
target_link_libraries(rcu_test rcu gtest_main)
add_test(NAME rcu_test COMMAND rcu_test)

add_executable(rcu_benchmark rcu_benchmark.cc)
target_link_libraries(rcu_benchmark rcu benchmark::benchmark_main)
add_test(NAME rcu_benchmark COMMAND rcu_benchmark)
