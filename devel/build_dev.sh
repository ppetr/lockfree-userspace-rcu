#!/bin/sh

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

set -e
BASE="$(realpath "$(dirname "$0")/..")"
mkdir -p "${BASE}/build"
echo "Signature: 8a477f597d28d172789f06886806bc55" >"${BASE}/build/CACHEDIR.TAG"

schedtool -B -n10 $$ || true

DIR="${BASE}/build/dev"
target="all"
mkdir -p "$DIR"
cd "$DIR"
while true ; do
  # When compiling with
  # -DCMAKE_TOOLCHAIN_FILE="$(pwd)/devel/toolchain-clang.cmake"
  # flag CMAKE_EXPORT_COMPILE_COMMANDS produces
  # build/dev/compile_commands.json. Symlink it to the project's root directory
  # to enable linting in editors that support this feature.
  CMAKE_EXPORT_COMPILE_COMMANDS=1 cmake "$@" "$BASE"
  if ! find "${BASE}/simple_rcu" -name '*.h' -or -name '*.cc' \
    | CTEST_OUTPUT_ON_FAILURE=1 entr -d /bin/sh -c "make -j$(nproc) ${target} && ctest -R '_test$'" ; then
    [ "$?" -eq 1 ] && break
  fi
  echo "Rerunning cmake"
  sleep 3
done
