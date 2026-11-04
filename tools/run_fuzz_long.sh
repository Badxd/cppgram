#!/usr/bin/env bash
# Builds the libFuzzer targets with clang and runs each one for a fixed time budget.
# Usage: tools/run_fuzz_long.sh [seconds_per_target]   (default 600). Output: build-fuzz/logs/<target>.log
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SECONDS_PER_TARGET="${1:-600}"
BUILD="$ROOT/build-fuzz"
LOGS="$BUILD/logs"
mkdir -p "$LOGS"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_C_COMPILER=clang-18 \
    -DENABLE_TESTS=OFF -DBUILD_FUZZERS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j1 --target fuzz_tl_reader fuzz_session_string fuzz_html_parser fuzz_session_crypto
for target in fuzz_tl_reader fuzz_session_string fuzz_html_parser fuzz_session_crypto; do
    corpus="$BUILD/corpus/$target"
    mkdir -p "$corpus"
    start=$(date +%s)
    set +e
    "$BUILD/$target" "$corpus" -max_total_time="$SECONDS_PER_TARGET" -timeout=10 -print_final_stats=1 \
        > "$LOGS/$target.log" 2>&1
    rc=$?
    set -e
    end=$(date +%s)
    echo "$target exit=$rc wall_seconds=$((end - start))" | tee -a "$LOGS/summary.txt"
done
