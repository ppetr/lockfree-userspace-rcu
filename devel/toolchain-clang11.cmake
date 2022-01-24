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

# Usage: -DCMAKE_TOOLCHAIN_FILE=toolchain-clang11.cmake
set(CLANG_VERSION 11)

set(CMAKE_C_COMPILER             "/usr/bin/clang-${CLANG_VERSION}")
set(CMAKE_C_FLAGS                "-Wall")
set(CMAKE_C_FLAGS_DEBUG          "-g")
set(CMAKE_C_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE        "-O3 -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g")

set(CMAKE_CXX_COMPILER             "/usr/bin/clang++-${CLANG_VERSION}")
set(CMAKE_CXX_FLAGS                "-Wall")
set(CMAKE_CXX_FLAGS_DEBUG          "-g")
set(CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE        "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")

set(LLVMAR_EXECUTABLE "/usr/bin/llvm-ar-${CLANG_VERSION}")
set(LLVMNM_EXECUTABLE "/usr/bin/llvm-nm-${CLANG_VERSION}")
set(LLVMRANLIB_EXECUTABLE "/usr/bin/llvm-ranlib-${CLANG_VERSION}")

set(CMAKE_OBJDUMP "/usr/bin/llvm-objdump-${CLANG_VERSION}")
set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld")

set(RUN_HAVE_STD_REGEX 0)
