#!/bin/sh

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

set -e
BASE="$(realpath "$(dirname "$0")/..")"
mkdir -p "${BASE}/build"
echo "Signature: 8a477f597d28d172789f06886806bc55" >"${BASE}/build/CACHEDIR.TAG"

for TOOLCHAIN in clang11 gcc ; do
  DIR="${BASE}/build/rel-${TOOLCHAIN}"
  mkdir -p "$DIR"
  cd "$DIR"
  cmake -DCMAKE_TOOLCHAIN_FILE="${BASE}/devel/toolchain-${TOOLCHAIN}.cmake" \
    -DBENCHMARK_ENABLE_LTO=true -DCMAKE_BUILD_TYPE=Release "$@" "$BASE"
  make -j all
done
cd "$BASE"
ls -l build/rel-*/simple_rcu/*_benchmark
