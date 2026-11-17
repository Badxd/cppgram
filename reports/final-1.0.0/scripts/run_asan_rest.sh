#!/usr/bin/env bash
set -u
ROOT=/tmp/claude-0/-home-claude/8b1f2cf3-d8a0-52e2-92a4-978325134559/scratchpad/v1/final
LOG="$ROOT/verify/logs"; S="$LOG/extra-status.txt"
cmake --build "$ROOT/bld-asan2" -j1 > "$LOG/x-asan-build-resume.log" 2>&1; echo "asan build (resume, -j1) rc=$?" >> "$S"
(cd "$ROOT/bld-asan2" && UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" ASAN_OPTIONS="detect_leaks=1" timeout 5400 ctest --output-on-failure -j2 > "$LOG/x-asan-ctest.log" 2>&1); rc=$?
echo "asan+ubsan ctest rc=$rc $(grep 'tests passed\|tests failed' "$LOG/x-asan-ctest.log" | tail -n1)" >> "$S"
grep -c "runtime error:\|ERROR: AddressSanitizer\|ERROR: LeakSanitizer" "$LOG/x-asan-ctest.log" > "$LOG/x-asan-findings.txt"
echo "ASAN DONE" >> "$S"
