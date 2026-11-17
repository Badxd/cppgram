#!/usr/bin/env bash
# Instrumented bounded libFuzzer run on the final tree. Library built with trace-pc-guard coverage (see run_all.sh s4).
set -u
ROOT=/tmp/claude-0/-home-claude/8b1f2cf3-d8a0-52e2-92a4-978325134559/scratchpad/v1/final
SRC="$ROOT/cppgram"; LOG="$ROOT/verify/logs"; B="$ROOT/bld-fuzz2"; C="$B/corpus"
mkdir -p "$LOG" "$C" "$B/seed-src/session"
: > "$LOG/s4-fuzz-summary.txt"
# libFuzzer (clang 18) rejects an explicit -fsanitize-coverage with -fsanitize=fuzzer. fuzzer-no-link alone instruments
# the library for coverage, so the flags below are the only instrumentation.
cmake -S "$SRC" -B "$B" -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_C_COMPILER=clang-18 -DCMAKE_BUILD_TYPE=None \
    -DENABLE_TESTS=OFF -DBUILD_FUZZERS=ON \
    "-DCMAKE_CXX_FLAGS=-O1 -g -fsanitize=fuzzer-no-link,address -fno-omit-frame-pointer" > "$LOG/s4-configure.log" 2>&1 || exit 1
cmake --build "$B" -j2 --target fuzz_tl_reader fuzz_session_string fuzz_html_parser fuzz_session_crypto \
    fuzz_plain_unwrap fuzz_decrypt_packet fuzz_transport_framing make_mtproto_seeds > "$LOG/s4-build.log" 2>&1 || exit 2
clang++-18 -std=c++20 -O1 -fsanitize=fuzzer-no-link,address \
    -I"$SRC/include" -I"$SRC/src" "$SRC/fuzz/make_session_seeds.cpp" "$B/libcppgram.a" \
    -lssl -lcrypto -lsqlite3 -lpthread -lz -o "$B/make_session_seeds" > "$LOG/s4-seed-build.log" 2>&1 || { echo "seed build failed"; exit 3; }
"$B/make_mtproto_seeds" "$B/seed-src" > "$LOG/s4-seeds-mtproto.log" 2>&1 || exit 4
"$B/make_session_seeds" "$B/seed-src/session" > "$LOG/s4-seeds-session.log" 2>&1 || exit 4
secs=120; fail=0
for t in fuzz_tl_reader fuzz_session_string fuzz_html_parser fuzz_session_crypto fuzz_plain_unwrap fuzz_decrypt_packet fuzz_transport_framing; do
    dir="$C/$t"; mkdir -p "$dir"
    case "$t" in
        fuzz_plain_unwrap) cp -r "$B/seed-src/plain/." "$dir/" ;;
        fuzz_decrypt_packet) cp -r "$B/seed-src/crypto/." "$dir/" ;;
        fuzz_transport_framing) cp -r "$B/seed-src/transport/." "$dir/" ;;
        fuzz_session_string) cp "$B/seed-src/session/"seed_* "$dir/" 2>/dev/null ;;
        fuzz_session_crypto) cp "$B/seed-src/session/"cge1_seed_* "$B/seed-src/session/"invalid_* "$dir/" 2>/dev/null ;;
    esac
    ASAN_OPTIONS="quarantine_size_mb=1:detect_leaks=0" timeout $((secs + 600)) "$B/$t" "$dir" \
        -max_total_time=$secs -timeout=10 -rss_limit_mb=2048 -print_final_stats=1 > "$LOG/s4-fuzz-$t.log" 2>&1
    rc=$?
    findings=$(grep -c "ERROR: libFuzzer\|ERROR: AddressSanitizer\|ERROR: UndefinedBehavior\|SUMMARY:" "$LOG/s4-fuzz-$t.log")
    execs=$(grep -o "stat::number_of_executed_units: [0-9]*" "$LOG/s4-fuzz-$t.log" | tail -n1)
    echo "fuzz $t rc=$rc findings=$findings $execs" >> "$LOG/s4-fuzz-summary.txt"
    { [ "$rc" -eq 0 ] && [ "$findings" -eq 0 ]; } || fail=1
done
echo "FUZZ DONE fail=$fail" >> "$LOG/s4-fuzz-summary.txt"
exit $fail
