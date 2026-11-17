# Test results: CppGram 1.0.0 (release candidate)

Every number here comes from a command in `reports/final-1.0.0/`. Each row cites its log there. Exit codes are the shell's
exit codes. Nothing here is a live Telegram result. No live Telegram validation has occurred in this release, and every
live check is BLOCKED on the sandbox network (see `KNOWN_LIMITATIONS.md`, RB-1). Results from earlier rounds are in
`reports/superseded/` and are not evidence for 1.0.0. Section 8 has the current live-project results.

## Environment

- Linux 6.18.44 (x86_64), 2 vCPU, 8 GB RAM. Ubuntu 24.04 userland.
- GCC 13.3.0 (`g++`, Ubuntu 13.3.0-6ubuntu2~24.04.1)
- Clang 18.1.3 (`clang++-18`, Ubuntu 1ubuntu1)
- CMake 3.28.3, Ninja 1.11.1, ccache in use for the release builds
- clang-tidy-18, clang-format-18.1.3, Doxygen 1.9.8
- Source: the final `cppgram/` tree. Its live-project digest is `57e313ea306c76c3f2de6cc97e6275b9e6664c1f53f3c4fb53daf409037ad58d`
  (`include/` and `src/`). Build logs confirm the tree was configured from this source.

## 1. Builds and the full test suite

| # | Command (run in `reports/final-1.0.0/scripts/run_all.sh`) | Exit | Result | Log |
|---|---|---|---|---|
| 1.1 | GCC 13.3 Release, `-DENABLE_TESTS=ON`: configure, `cmake --build`, warnings counted | 0 | 0 warnings | `logs/s0-configure.log`, `s0-build.log`, `s0-warnings.txt` |
| 1.2 | GCC: `ctest -j2` | 0 | 711 of 711 passed, 0 failed, 65.21 s | `logs/s0-ctest.log` |
| 1.3 | Clang 18.1.3 Release: configure and build | 0 | 0 warnings | `logs/s1-configure.log`, `s1-build.log` |
| 1.4 | Clang: `ctest -j2` | 0 | 711 of 711 passed, 0 failed, 67.42 s | `logs/s1-ctest.log` |

Test counts per binary (from the `[  PASSED  ]` lines of the sanitizer runs): `cppgram_mtproto_test` 457,
`cppgram_bot_test` 110, `cppgram_async_test` 46, `cppgram_security_test` 53, `cppgram_metrics_test` 23,
`cppgram_media_test` 22. Total 711, matching CTest.

## 2. Schema

| # | Command | Exit | Result | Log |
|---|---|---|---|---|
| 2.1 | `cmake --build <clang build> --target tl-check` (regenerates TL code into a temp dir and diffs it) | 0 | 19 `api_impl` files match the generator; `schema/SHA256SUMS.txt` 24 entries match | `logs/s2-schema-check.log` |

The checked-in `api_impl_*.cpp` files were regenerated before this run. The only difference from the previous files was
numbered lambda parameter names. The CHANGELOG records this.

## 3. Sanitizers

| # | Command | Exit | Result | Log |
|---|---|---|---|---|
| 3.1 | TSan, Clang 18.1.3, `-fsanitize=thread`, `cppgram_bot_test` | 0 | 110 passed, 0 ThreadSanitizer reports | `logs/s3-tsan-cppgram_bot_test.log` |
| 3.2 | TSan, `cppgram_security_test` | 0 | 53 passed, 0 reports | `logs/s3-tsan-cppgram_security_test.log` |
| 3.3 | TSan, `cppgram_metrics_test` | 0 | 23 passed, 0 reports | `logs/s3-tsan-cppgram_metrics_test.log` |
| 3.4 | TSan, `cppgram_media_test` | 0 | 22 passed, 0 reports | `logs/s3-tsan-cppgram_media_test.log` |
| 3.5 | TSan, `cppgram_async_test` | 0 | 46 passed, 0 reports | `logs/x-tsan-cppgram_async_test.log` |
| 3.6 | TSan, `cppgram_mtproto_test` | 0 | 457 passed, 0 reports | `logs/x-tsan-cppgram_mtproto_test.log` |
| 3.7 | ASan + UBSan, Clang 18.1.3, `-O0 -fsanitize=address,undefined -fno-omit-frame-pointer`, full build | 0 | build finished 163 of 163 steps on the second attempt (see 3.8) | `logs/x-asan-build-resume.log` |
| 3.8 | First ASan build attempt, parallel `-j2` | 2 | One object (`api_impl_2.cpp.o`) failed with no diagnostic. A standalone recompile of that file exited 0. The build was then resumed serially with `-j1`. | `logs/x-asan-build.log`, `logs/x-asan-api_impl_2.log` |
| 3.9 | `ctest -j2` under ASan and UBSan (with `UBSAN_OPTIONS=halt_on_error=1`, `ASAN_OPTIONS=detect_leaks=1`) | 0 | 711 of 711 passed, 0 failed, 116.24 s. Log scan for `runtime error`, `AddressSanitizer`, `LeakSanitizer`: 0 | `logs/x-asan-ctest.log`, `logs/x-asan-findings.txt` |

TSan covers all six test binaries. Together they run 711 tests with no reports. MSan was not run.

## 4. Fuzzing (libFuzzer, Clang 18.1.3, `BUILD_FUZZERS=ON`)

The library itself is built with `-fsanitize=fuzzer-no-link,address`, so its code is coverage-instrumented along with the
harnesses. Each target ran once for 120 s on the final tree, with corpora seeded from `make_mtproto_seeds` and
`make_session_seeds`. Logs are in `logs/s4-fuzz-<target>.log`. Summary: `logs/s4-fuzz-summary.txt`.

| Target | Exit | Executions | Covered edges (`cov`) | Features (`ft`) | Corpus files at end | Peak RSS (MB) | Findings |
|---|---|---|---|---|---|---|---|
| fuzz_tl_reader | 0 | 2,909,601 | 76 | 413 | 149 | 51 | 0 |
| fuzz_session_string | 0 | 25,323,564 | 78 | 239 | 209 | 50 | 0 |
| fuzz_html_parser | 0 | 979,235 | 832 | 5,735 | 1,315 | 66 | 0 |
| fuzz_session_crypto | 0 | 7,184 | 67 | 106 | 49 | 83 | 0 |
| fuzz_plain_unwrap | 0 | 1,989,665 | 115 | 123 | 26 | 53 | 0 |
| fuzz_decrypt_packet | 0 | 1,566,108 | 139 | 242 | 47 | 56 | 0 |
| fuzz_transport_framing | 0 | 4,186,649 | 104 | 428 | 127 | 53 | 0 |

Total: 7 campaigns, 36,962,006 executions, 7 × 120 s budgets (libFuzzer reported 121 s per run), 0 findings.

Notes:
- `cov` and `ft` are libFuzzer's counts of covered program counters and features. No total was measured, so these are not
  percentages of code coverage.
- `fuzz_session_crypto` ran only 7,184 executions in 120 s (about 59 per second), because each input does authenticated
  key derivation. Its result is a short run and is not a sign of coverage.
- The first attempt at this stage exited 3 because the seed writer did not link with the coverage runtime. A second
  attempt failed because libFuzzer rejects an explicit `-fsanitize-coverage` flag combined with `-fsanitize=fuzzer`.
  The third attempt, with `-fsanitize=fuzzer-no-link` alone, produced the results above. The logs of the two failed
  attempts were overwritten by the successful run, so they are described here only.
- The fuzz runs used `ASAN_OPTIONS=quarantine_size_mb=1:detect_leaks=0`. Leak checking is covered by the ASan suite in section 3.
- Runs are bounded. `tools/run_fuzz_long.sh` with its default budgets was not run.

## 5. Install and independent consumer

| # | Command | Exit | Result | Log |
|---|---|---|---|---|
| 5.1 | GCC: `cmake --install` | 0 | installed under `lib/cmake/CppGram` | `logs/s0-install.log` |
| 5.2 | GCC consumer (`verify/consumer`): `find_package(CppGram 1.0.0 EXACT)`, `-Werror` build | 0 | built | `logs/s0-consumer-configure.log`, `s0-consumer-build.log` |
| 5.3 | GCC consumer run | 0 | `consumer ok: app_version 1.0.0, offline API calls succeeded` | `logs/s0-consumer-run.log` |
| 5.4 | Clang: install, consumer build and run | 0 | same output | `logs/s1-install.log`, `s1-consumer-*.log` |

## 6. Project checks (`make checks`)

| Check | Exit | Result | Log |
|---|---|---|---|
| CMake source lists | 0 | library 112 named, 112 on disk, 0 problems; tests 68 named, 68 on disk, 0 problems | `logs/s7-make-checks.log` |
| Version consistency | 0 | VERSION is 1.0.0; client.h app_version matches; CHANGELOG has a 1.0.0 heading | same |
| API index | 0 | 476 distinct Client members, all defined | same |
| Formatting (clang-format 18.1.3) | 0 | 56 files, 0 failing | same |
| Examples parse against the public headers | 0 | 16 example files OK | same |

`make checks` ran with exit code 0. Its 21 `OK` lines are 16 example files, 3 version checks, 1 API index check and 1 bare `OK` from the CMake source check. Full output: `logs/s7-make-checks.log`.

## 7. Static analysis and documentation

| Tool | Command | Exit | Result | Log |
|---|---|---|---|---|
| clang-tidy-18 | checks `bugprone-*,clang-analyzer-*,performance-*` minus two noisy checks, every non-schema `src/*.cpp` | 0 | 304 warnings, mostly `performance-unnecessary-value-param`. Triage of the 12 `bugprone` findings: no confirmed defect. | `logs/s5-tidy-raw.log`, `s5-tidy-count.txt` |
| Doxygen 1.9.8 | `cmake -DBUILD_DOCUMENTATION=ON` then `cmake --build <docs build> --target doc_doxygen` | 0 | docs built, 899 warnings, mostly unknown-command and HTML-like tag markup in comments. Rerun after the output path change (below): same 899 warnings. | `logs/s6-doxygen.log` (first run, output inside the source tree), `logs/s6b-doxygen.log` (rerun, output in the build tree) |

Doxygen output used to be written to `doc/` in the source tree, which put 188 MB of generated HTML into the source. It now goes to `<build>/doc/`. The generated `doc/` directory was deleted from the source tree.

The clang-tidy triage is in `reports/final-1.0.0/logs/s5-tidy-raw.log`. Warnings were not fixed in this release.

## 8. Live-test project (`CppGramLiveTest`)

| Run | Command | Exit | Result | Log |
|---|---|---|---|---|
| Bot unit tests | `livebot_unit_tests` | 0 | 136 checks, 0 failures (includes deliberate redaction fixtures) | `live/livebot-unit.log` |
| Runner, default mode (current, after the network-gate fix) | `cppgram_livetest --workdir <dir> --json <file>`, no environment | 0 | 4 PASS (offline and environment), 12 BLOCKED, 0 PENDING. Every line has `telegram_contact=no`. Probe intercepted on this network. | `CppGramLiveTest/reports/logs/gate-fix/a-default-no-env.out`, `.json` |
| Runner, interception override (current) | `CGLT_NETWORK_UNRESTRICTED=1 cppgram_livetest --category live_no_credentials` | 0 | 6 of 6 live checks BLOCKED with `telegram_contact=no`. The override does not bypass the interception gate. | `CppGramLiveTest/reports/logs/gate-fix/c-no-creds-override.out`, `.json` |
| Runner, credentials and owner checks with override (current) | `live_credentials` and `owner_approval` with `CGLT_NETWORK_UNRESTRICTED=1` and fixture values | 0 | 2 + 2 + 3 + 3 checks BLOCKED with `telegram_contact=no`. Strace shows the only non-local connect is the probe address. | `CppGramLiveTest/reports/logs/gate-fix/` |
| Gate regression tests | `livetest_gate_tests` | 0 | 63 checks, 0 failures | `CppGramLiveTest/reports/logs/gate-fix/j-gate-tests.out` |
| Bot startup, override (current) | `cppgram_live_test_bot` with fixture values | 3 | refused by the gate before config loading | `CppGramLiveTest/reports/logs/gate-fix/f-bot-startup.out` |

The default-mode row "4 PASS, 9 BLOCKED, 3 PENDING" in `live/livetest-default-mode.log` is from the run before the
network-gate fix. It is superseded. Before the fix, the owner checks were reported as PENDING on an intercepted network, and
the gate did not cover `live_credentials`. The `live/` logs are kept as history.

The live-project details are in `CppGramLiveTest/TEST_RESULTS.md`.

## 9. Not run

- ARM64 build and tests.
- MSan.
- Long fuzz campaigns (`tools/run_fuzz_long.sh` default budgets).
- Controlled FLOOD_WAIT on the live network.
- Every live Telegram check. They are BLOCKED on this network and need an unrestricted one.
- GitHub Actions (`.github/workflows/`): not run.
- `tools/mtproxy_harness` (Python reference vectors and the C++ dump): not run in this release.
- `tools/run_sanitizers.sh` and the `make` sanitizer targets: not run as scripts. The same sanitizer builds were run directly (section 3).
