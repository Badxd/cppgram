#!/usr/bin/env bash
# Final local verification pipeline for CppGram 1.0.0. Runs stages in order; each stage writes a status line.
# Nothing here contacts Telegram. Live runs use the default (gated) mode with no credentials.
set -u
ROOT=/tmp/claude-0/-home-claude/8b1f2cf3-d8a0-52e2-92a4-978325134559/scratchpad/v1/final
SRC="$ROOT/cppgram"
V="$ROOT/verify"
LOG="$V/logs"
STATUS="$V/status.txt"
mkdir -p "$LOG"
: > "$STATUS"
J=2

stage() { # name, function
    local name="$1"; shift
    echo "START $name $(date -u +%FT%TZ)" >> "$STATUS"
    "$@"
    local rc=$?
    echo "END   $name rc=$rc $(date -u +%FT%TZ)" >> "$STATUS"
}

s0_gcc_release() {
    cmake -S "$SRC" -B "$ROOT/bld-gcc-final" -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc \
        -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DCMAKE_INSTALL_PREFIX="$ROOT/bld-gcc-final-install" \
        > "$LOG/s0-configure.log" 2>&1 || return 1
    cmake --build "$ROOT/bld-gcc-final" -j$J > "$LOG/s0-build.log" 2>&1 || return 2
    grep -c "warning:" "$LOG/s0-build.log" > "$LOG/s0-warnings.txt"
    (cd "$ROOT/bld-gcc-final" && ctest --output-on-failure -j$J > "$LOG/s0-ctest.log" 2>&1) || return 3
    cmake --install "$ROOT/bld-gcc-final" > "$LOG/s0-install.log" 2>&1 || return 4
    cmake -S "$SRC/verify/consumer" -B "$ROOT/bld-gcc-final-consumer" -G Ninja \
        -DCMAKE_PREFIX_PATH="$ROOT/bld-gcc-final-install" -DCMAKE_CXX_COMPILER=g++ > "$LOG/s0-consumer-configure.log" 2>&1 || return 5
    cmake --build "$ROOT/bld-gcc-final-consumer" > "$LOG/s0-consumer-build.log" 2>&1 || return 6
    "$ROOT/bld-gcc-final-consumer/consumer" > "$LOG/s0-consumer-run.log" 2>&1 || return 7
    return 0
}

s1_clang_release() {
    cmake -S "$SRC" -B "$ROOT/bld-clang2" -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_C_COMPILER=clang-18 \
        -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DCMAKE_INSTALL_PREFIX="$ROOT/bld-clang2-install" > "$LOG/s1-configure.log" 2>&1 || return 1
    cmake --build "$ROOT/bld-clang2" -j$J > "$LOG/s1-build.log" 2>&1 || return 2
    (cd "$ROOT/bld-clang2" && ctest --output-on-failure -j$J > "$LOG/s1-ctest.log" 2>&1) || return 3
    cmake --install "$ROOT/bld-clang2" > "$LOG/s1-install.log" 2>&1 || return 4
    cmake -S "$SRC/verify/consumer" -B "$ROOT/bld-clang2-consumer" -G Ninja \
        -DCMAKE_PREFIX_PATH="$ROOT/bld-clang2-install" -DCMAKE_CXX_COMPILER=clang++-18 \
        > "$LOG/s1-consumer-configure.log" 2>&1 || return 5
    cmake --build "$ROOT/bld-clang2-consumer" > "$LOG/s1-consumer-build.log" 2>&1 || return 6
    "$ROOT/bld-clang2-consumer/consumer" > "$LOG/s1-consumer-run.log" 2>&1 || return 7
    return 0
}

s2_schema() {
    cmake --build "$ROOT/bld-clang2" --target tl-check > "$LOG/s2-schema-check.log" 2>&1
}

s3_tsan() {
    cmake -S "$SRC" -B "$ROOT/bld-tsan" -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_C_COMPILER=clang-18 \
        -DCMAKE_BUILD_TYPE=None -DENABLE_TESTS=ON \
        "-DCMAKE_CXX_FLAGS=-O1 -g0 -fsanitize=thread -fno-omit-frame-pointer" \
        "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread" "-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=thread" \
        > "$LOG/s3-configure.log" 2>&1 || return 1
    cmake --build "$ROOT/bld-tsan" -j$J --target cppgram_bot_test cppgram_security_test cppgram_metrics_test \
        cppgram_media_test > "$LOG/s3-build.log" 2>&1 || return 2
    local fail=0 t
    for t in cppgram_bot_test cppgram_security_test cppgram_metrics_test cppgram_media_test; do
        TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1" timeout 1800 \
            "$ROOT/bld-tsan/tests/$t" > "$LOG/s3-tsan-$t.log" 2>&1
        local rc=$?
        local reports; reports=$(grep -c "WARNING: ThreadSanitizer" "$LOG/s3-tsan-$t.log")
        echo "tsan $t rc=$rc reports=$reports" >> "$LOG/s3-tsan-summary.txt"
        [ "$rc" -eq 0 ] && [ "$reports" -eq 0 ] || fail=1
    done
    return $fail
}

s4_fuzz() {
    cmake -S "$SRC" -B "$ROOT/bld-fuzz2" -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_C_COMPILER=clang-18 \
        -DCMAKE_BUILD_TYPE=None -DENABLE_TESTS=OFF -DBUILD_FUZZERS=ON \
        "-DCMAKE_CXX_FLAGS=-O1 -g -fsanitize=fuzzer-no-link,address -fsanitize-coverage=trace-pc-guard,trace-cmp -fno-omit-frame-pointer" \
        > "$LOG/s4-configure.log" 2>&1 || return 1
    cmake --build "$ROOT/bld-fuzz2" -j$J --target fuzz_tl_reader fuzz_session_string fuzz_html_parser \
        fuzz_session_crypto fuzz_plain_unwrap fuzz_decrypt_packet fuzz_transport_framing make_mtproto_seeds \
        > "$LOG/s4-build.log" 2>&1 || return 2
    # Session seed writer is not a CMake target (see fuzz/README.md). Build it by hand against the instrumented library.
    clang++-18 -std=c++20 -O1 -I"$SRC/include" -I"$SRC/src" "$SRC/fuzz/make_session_seeds.cpp" \
        "$ROOT/bld-fuzz2/libcppgram.a" -lssl -lcrypto -lsqlite3 -lpthread -lz -o "$ROOT/bld-fuzz2/make_session_seeds" \
        > "$LOG/s4-seed-build.log" 2>&1 || return 3
    local B="$ROOT/bld-fuzz2" C="$ROOT/bld-fuzz2/corpus"
    mkdir -p "$C" "$B/seed-src/session"
    "$B/make_mtproto_seeds" "$B/seed-src" > "$LOG/s4-seeds-mtproto.log" 2>&1 || return 4
    "$B/make_session_seeds" "$B/seed-src/session" > "$LOG/s4-seeds-session.log" 2>&1 || return 4
    local secs=120 fail=0 t sub
    for t in fuzz_tl_reader fuzz_session_string fuzz_html_parser fuzz_session_crypto fuzz_plain_unwrap \
             fuzz_decrypt_packet fuzz_transport_framing; do
        local dir="$C/$t"; mkdir -p "$dir"
        case "$t" in
            fuzz_plain_unwrap) cp -r "$B/seed-src/plain/." "$dir/" ;;
            fuzz_decrypt_packet) cp -r "$B/seed-src/crypto/." "$dir/" ;;
            fuzz_transport_framing) cp -r "$B/seed-src/transport/." "$dir/" ;;
            fuzz_session_string) cp "$B/seed-src/session/"seed_* "$dir/" 2>/dev/null ;;
            fuzz_session_crypto) cp "$B/seed-src/session/"cge1_seed_* "$B/seed-src/session/"invalid_* "$dir/" 2>/dev/null ;;
        esac
        ASAN_OPTIONS="quarantine_size_mb=1:detect_leaks=0" timeout $((secs + 600)) "$B/$t" "$dir" \
            -max_total_time=$secs -timeout=10 -rss_limit_mb=2048 -print_final_stats=1 \
            > "$LOG/s4-fuzz-$t.log" 2>&1
        local rc=$?
        local crashes; crashes=$(grep -c "ERROR: libFuzzer\|ERROR: AddressSanitizer\|ERROR: UndefinedBehavior\|SUMMARY:" "$LOG/s4-fuzz-$t.log")
        local execs; execs=$(grep -o "stat::number_of_executed_units: [0-9]*" "$LOG/s4-fuzz-$t.log" | tail -n1)
        echo "fuzz $t rc=$rc findings=$crashes $execs" >> "$LOG/s4-fuzz-summary.txt"
        [ "$rc" -eq 0 ] && [ "$crashes" -eq 0 ] || fail=1
    done
    return $fail
}

s5_tidy() {
    local files
    files=$(cd "$SRC" && find src -name "*.cpp" -not -path "src/mtproto/schema/*" | sort)
    (cd "$SRC" && printf '%s\n' $files | xargs -P$J -I{} clang-tidy-18 -p "$ROOT/bld-clang2" --quiet \
        -checks='bugprone-*,clang-analyzer-*,performance-*,-bugprone-easily-swappable-parameters,-performance-enum-size' \
        {} > "$LOG/s5-tidy-raw.log" 2>&1)
    grep -c "warning:" "$LOG/s5-tidy-raw.log" > "$LOG/s5-tidy-count.txt"
    return 0
}

s6_docs() {
    if ! command -v doxygen >/dev/null 2>&1; then
        DEBIAN_FRONTEND=noninteractive apt-get install -y doxygen > "$LOG/s6-apt.log" 2>&1 || return 1
    fi
    doxygen --version > "$LOG/s6-doxygen-version.txt" 2>&1
    cmake -S "$SRC" -B "$ROOT/bld-docs" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_DOCUMENTATION=ON \
        > "$LOG/s6-configure.log" 2>&1 || return 2
    cmake --build "$ROOT/bld-docs" --target doc_doxygen > "$LOG/s6-doxygen.log" 2>&1 || return 3
    grep -c "warning:" "$LOG/s6-doxygen.log" > "$LOG/s6-doxygen-warnings.txt"
    return 0
}

s7_checks() {
    (cd "$SRC" && make checks) > "$LOG/s7-make-checks.log" 2>&1
}

s8_live_default() {
    cmake -S "$ROOT/CppGramLiveTest" -B "$ROOT/bld-live2" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER=clang++-18 -DCPPGRAM_SOURCE_DIR="$SRC" -DCPPGRAM_PREBUILT_DIR="$ROOT/bld-clang2" \
        > "$LOG/s8-configure.log" 2>&1 || return 1
    cmake --build "$ROOT/bld-live2" -j$J > "$LOG/s8-build.log" 2>&1 || return 2
    local bin; bin=$(find "$ROOT/bld-live2" -maxdepth 2 -type f -name cppgram_livetest | head -n1)
    [ -n "$bin" ] || return 3
    env -u TG_API_ID -u TG_API_HASH -u TG_BOT_TOKEN -u TG_TEST_CHAT_ID -u CGLT_OWNER_APPROVED \
        -u CGLT_NETWORK_UNRESTRICTED -u CGLT_PROXY_HOST -u CGLT_PROXY_PORT -u CGLT_PROXY_SECRET \
        timeout 900 "$bin" > "$LOG/s8-livetest-default.log" 2>&1
    echo "livetest default rc=$?" >> "$LOG/s8-summary.txt"
    return 0
}

stage s0_gcc_release s0_gcc_release
stage s1_clang_release s1_clang_release
stage s2_schema s2_schema
stage s3_tsan s3_tsan
stage s4_fuzz s4_fuzz
stage s5_tidy s5_tidy
stage s6_docs s6_docs
stage s7_checks s7_checks
stage s8_live_default s8_live_default
echo "PIPELINE DONE $(date -u +%FT%TZ)" >> "$STATUS"
