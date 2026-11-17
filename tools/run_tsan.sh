#!/usr/bin/env bash
# Builds the test suite with ThreadSanitizer in build-tsan/ and runs the concurrency tests.
# -O1 -g0 and -j1: the api_impl_*.cpp files are OOM-killed on an 8 GB machine with -j2. Expect 1.5 h or more.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$ROOT" -B "$ROOT/build-tsan" -DCMAKE_BUILD_TYPE=None -DENABLE_TESTS=ON \
    -DCMAKE_CXX_FLAGS="-O1 -g0 -fsanitize=thread -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=thread"
cmake --build "$ROOT/build-tsan" -j1 --target cppgram_mtproto_test
# Concurrency-focused tests first; a full run follows if these pass.
TSAN_OPTIONS="halt_on_error=1:second_deadlock_stack=1" "$ROOT/build-tsan/tests/cppgram_mtproto_test" \
    --gtest_filter='CancelStress*:Cancel*:Socks5*:SessionCrypto*'
TSAN_OPTIONS="halt_on_error=1" "$ROOT/build-tsan/tests/cppgram_mtproto_test"
