# Copyright 2022-2025 Google LLC
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

# Usage: -DCMAKE_TOOLCHAIN_FILE=toolchain-clang.cmake
#set(CLANG_VERSION 18)

set(CMAKE_C_COMPILER             "/usr/bin/clang")
set(CMAKE_C_FLAGS                "-Wall")
set(CMAKE_C_FLAGS_DEBUG          "-g")
set(CMAKE_C_FLAGS_MINSIZEREL     "-Os -DNDEBUG -flto=auto")
set(CMAKE_C_FLAGS_RELEASE        "-O3 -DNDEBUG -flto=auto")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -flto=auto")

# TSAN: Add `-fsanitize=thread -g` [https://clang.llvm.org/docs/ThreadSanitizer.html]
# ASAN: `-fsanitize=address -fno-omit-frame-pointer`
set(CMAKE_CXX_COMPILER             "/usr/bin/clang++")
set(CMAKE_CXX_FLAGS                "-Wall")
set(CMAKE_CXX_FLAGS_DEBUG          "-g -fsanitize=address -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG -flto=auto")
set(CMAKE_CXX_FLAGS_RELEASE        "-O3 -DNDEBUG -flto=auto -fPIC -ftls-model=initial-exec")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -flto=auto")

set(LINKER_TYPE LLD)
set(LLVMAR_EXECUTABLE "/usr/bin/llvm-ar")
set(LLVMNM_EXECUTABLE "/usr/bin/llvm-nm")
set(LLVMRANLIB_EXECUTABLE "/usr/bin/llvm-ranlib")

set(CMAKE_OBJDUMP "/usr/bin/llvm-objdump")
set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld")

set(RUN_HAVE_STD_REGEX 0)
