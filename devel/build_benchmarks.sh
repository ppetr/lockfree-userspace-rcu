#!/bin/sh
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
ls -l build/rel-*/*_benchmark
