#!/usr/bin/env bash
# Re-runs the sanitizers on the final tree: TSan for the two concurrency binaries not yet rerun, ASan+UBSan for the full suite.
set -u
ROOT=/tmp/claude-0/-home-claude/8b1f2cf3-d8a0-52e2-92a4-978325134559/scratchpad/v1/final
SRC="$ROOT/cppgram"; LOG="$ROOT/verify/logs"; S="$LOG/extra-status.txt"; : > "$S"
cmake --build "$ROOT/bld-tsan" -j2 --target cppgram_async_test cppgram_mtproto_test > "$LOG/x-tsan-build.log" 2>&1 || { echo "tsan build rc=2" >> "$S"; exit 2; }
for t in cppgram_async_test cppgram_mtproto_test; do
    TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1" timeout 2400 "$ROOT/bld-tsan/tests/$t" > "$LOG/x-tsan-$t.log" 2>&1
    rc=$?; rep=$(grep -c "WARNING: ThreadSanitizer" "$LOG/x-tsan-$t.log"); pass=$(grep -E "^\[  PASSED  \]" "$LOG/x-tsan-$t.log" | tail -n1)
    echo "tsan $t rc=$rc reports=$rep $pass" >> "$S"
done
cmake -S "$SRC" -B "$ROOT/bld-asan2" -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_C_COMPILER=clang-18 \
    -DCMAKE_BUILD_TYPE=None -DENABLE_TESTS=ON "-DCMAKE_CXX_FLAGS=-O0 -g0 -fsanitize=address,undefined -fno-omit-frame-pointer" \
    "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined" "-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined" > "$LOG/x-asan-configure.log" 2>&1 || { echo "asan configure rc=1" >> "$S"; exit 1; }
cmake --build "$ROOT/bld-asan2" -j2 > "$LOG/x-asan-build.log" 2>&1 || { echo "asan build rc=2" >> "$S"; exit 2; }
(cd "$ROOT/bld-asan2" && UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" ASAN_OPTIONS="detect_leaks=1" ctest --output-on-failure -j2 > "$LOG/x-asan-ctest.log" 2>&1)
echo "asan+ubsan ctest rc=$? $(grep 'tests passed' "$LOG/x-asan-ctest.log")" >> "$S"
grep -c "runtime error:\|ERROR: AddressSanitizer\|ERROR: LeakSanitizer" "$LOG/x-asan-ctest.log" > "$LOG/x-asan-findings.txt"
echo "EXTRA DONE" >> "$S"
