#!/bin/sh
set -e
BASE="$(realpath "$(dirname "$0")/..")"
mkdir -p "${BASE}/build"
echo "Signature: 8a477f597d28d172789f06886806bc55" >"${BASE}/build/CACHEDIR.TAG"

DIR="${BASE}/build/dev"
mkdir -p "$DIR"
cd "$DIR"
cmake "$@" "$BASE"
while echo Restarting ; sleep 1 ; do
  find "$BASE" -name '*.h' -or -name '*.cc' \
    | CTEST_OUTPUT_ON_FAILURE=1 entr -d make -j all -k test
done 
