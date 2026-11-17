#!/usr/bin/env bash
# Builds the test suite with ASan + UBSan in build-san/ and runs it. Needs ~8 GB RAM for a -j1 build (about 1.5 h on a small VPS).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# No debug info (-g0): with -g the api_impl_*.cpp compiles are OOM-killed on 8 GB machines.
cmake -S "$ROOT" -B "$ROOT/build-san" -DCMAKE_BUILD_TYPE=None -DENABLE_TESTS=ON \
    -DCMAKE_CXX_FLAGS="-O0 -g0 -fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build "$ROOT/build-san" -j1
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir "$ROOT/build-san" --output-on-failure
