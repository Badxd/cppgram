#!/usr/bin/env bash
# Builds and runs cppgram_bench in a Release build, and prints the environment next to the numbers.
# Usage: tools/run_benchmarks.sh [build-dir]    (default: build/bench)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build/bench}"

cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release -DENABLE_BENCHMARKS=ON -DENABLE_TESTS=OFF >/dev/null
cmake --build "$build" --target cppgram_bench -j"$(nproc)" >/dev/null

echo "== environment"
echo "date:      $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "os:        $(. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-unknown}") / $(uname -sr)"
echo "cpu:       $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //') ($(nproc) threads)"
echo "memory:    $(awk '/MemTotal/ {printf "%.1f GiB", $2/1048576}' /proc/meminfo)"
echo "compiler:  $(c++ --version | head -1)"
echo "build:     $(grep '^CMAKE_BUILD_TYPE:' "$build/CMakeCache.txt" | cut -d= -f2)"
echo "flags:     $(grep '^CMAKE_CXX_FLAGS_RELEASE:' "$build/CMakeCache.txt" | cut -d= -f2-)"
echo "tmpfs:     $(df -T "${TMPDIR:-/tmp}" | awk 'NR==2 {print $2}') (session and sqlite cases use this file system)"
echo "load:      $(cut -d' ' -f1-3 /proc/loadavg)"
echo
echo "== results"
"$build/cppgram_bench"
