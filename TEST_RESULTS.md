# Test results: CppGram 0.1.0 (release-verification round, baseline 1cd09d0)

Log paths are relative to the evidence package root. Status values: PASS, FAIL, BLOCKED, NOT OBSERVED, NOT RUN,
PENDING USER APPROVAL.

## 1. Offline unit and integration tests (CTest, 711 cases)

| Binary | Test cases (GCC, clean) | Test cases (Clang, clean) | Status |
|---|---|---|---|
| cppgram_mtproto_test | 457 | 457 | PASS |
| cppgram_async_test | 46 | 46 | PASS |
| cppgram_bot_test | 110 | 110 | PASS |
| cppgram_media_test | 22 | 22 | PASS |
| cppgram_metrics_test | 23 | 23 | PASS |
| cppgram_security_test | 53 | 53 | PASS |
| **Total** | **711** | **711** | **PASS** |

CTest totals: GCC `711/711` (`logs/ctest-gcc.log`, CTEST=0). Clang `711/711` (`logs/ctest-clang.log`, CTEST=0).
The MTProto binary total is 457 on both compilers, matching the CTest count for that binary family.

### Why the totals changed from 700/700 and 446/446

The earlier accepted checkpoint reported 700 CTest cases and 446 MTProto-binary cases. The counts below come from
counting `TEST(`, `TEST_F(` and `TEST_P(` macros in the source (`tests/**/*.cpp`) and from the test names in each tree.
The three trees are the baseline commit 1cd09d0, the pre-mission backup (`backup-work-tree-before-mission4.tar.gz`,
the state before this round), and the current tree.

| Tree | All test macros | MTProto test macros |
|---|---|---|
| Baseline 1cd09d0 | 696 | 442 |
| Pre-mission backup (this round's start) | 702 | 448 |
| Current tree (this round's end) | 711 | 457 |

Reconciliation:
1. Baseline → pre-mission backup: +6 MTProto tests, added in the earlier round, not this round:
   `Crypto.EveryTruncationOfAnEncryptedPacketIsRejected`, `Crypto.RandomEncryptedPacketsAreRejectedWithoutCrashing`,
   `Session.FloodWaitSleepHonoursPerRequestCancel`, `Session.FloodWaitSleepIsNotTakenPastRequestDeadline`,
   `TransportFraming.IntermediateRejectsFrameOneByteOverMaxFrame`,
   `TransportFraming.IntermediateAcceptsFrameExactlyAtMaxFrameBoundary`.
2. Pre-mission backup → current tree: +9 MTProto tests, added in this round (names from a per-test diff):
   - `CryptoAudit` (6): `ExactLiveTrailingCounts22And102EncryptedAreIgnored`, `ForeignAuthKeyIdIsRejected`,
     `InvalidMsgKeyEveryByteIsRejected`, `OversizedInnerLengthIsRejectedWithoutReading`,
     `PacketUnderAnotherAuthKeyIsRejected`, `ValidBoundaryBodySizesRoundTrip`.
   - `ClientFakeDc.DropRecoveryReusesAuthKeyWithoutNewHandshake` (1).
   - `Session.FloodWaitRetryLoopIsBoundedByAttemptBudget` (1).
   - `Retry.FloodWaitDigitBoundaryAndStrictFormat` (1).
   No existing test was removed (the removed-test set is empty).
3. The checkpoint reported 446 and 700, which is 2 below the pre-mission backup (448 and 702). Those two tests were
   added after the checkpoint was accepted, in the earlier round. The preserved artifacts do not show which two; the
   checkpoint tree was not kept. This is stated as a limitation, not inferred.

Net change from the checkpoint: 700 → 711 (+11) and 446 → 457 (+11). That is 2 tests from the earlier round that the
checkpoint did not count, plus the 9 tests added in this round. All 11 are in the MTProto binary.

Note that the macro counts (696, 702, 711) are source counts. The CTest counts (711) equal the source count for the
current tree, and the per-binary gtest totals equal the per-binary source counts. The 696 and 702 values are source
counts only; the baseline and pre-mission CTest totals were not run in this round.

## 2. Targeted sanitizer runs (mtproto binary only)

All sanitizer binaries were built fresh from the current source tree in this round. Earlier sanitizer logs were NOT
reused: some test files were edited after those logs were written, so they no longer describe the current tree.

| Run | Build flags | Filter (gtest) | Result | Log |
|---|---|---|---|---|
| ASan + UBSan | clang++-18, Release, `ENABLE_SANITIZERS=ON` (`-fsanitize=address,undefined -fno-omit-frame-pointer`) | `CryptoAudit.*:Crypto.*:Session.FloodWait*:Retry.FloodWait*:ClientFakeDc.*:TransportFraming.*:DcMigration*:Socks*:ProxyStub*:ProxySecret*` | 118 PASS, exit 0, no sanitizer report | `logs/sanitizers/asan-ubsan.log` (build: `asan-ubsan-build.log`) |
| TSan | clang++-18, Release, `-DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"`, linker `-fsanitize=thread` | `Session.*:ClientFakeDc.*:Cancel*:InFlightCancel*:Connection*:CoroutineCancellation*` | 47 PASS, exit 0, 0 ThreadSanitizer warnings | `logs/sanitizers/tsan.log` (build: `tsan-build.log`) |

Instrumentation was confirmed: the ASan binary contains `__asan_*` and `__ubsan_*` symbols; the TSan binary contains `__tsan_init`.

Not run this round: sanitizer runs of the async, bot, media, metrics and security binaries, and of the full mtproto
suite. Earlier-round sanitizer counts (for example 150/150 and 80/80) are not reproduced here and are not claimed.

## 3. Fuzzing

Full table: `logs/fuzz-results.log` (generated from the logs by `logs/gen_summaries.py`).

| Target | Build | Executions | Duration | Exit | Crash or sanitizer report |
|---|---|---|---|---|---|
| fuzz_plain_unwrap | ASan + UBSan, `-fsanitize=fuzzer-no-link` (instrumented campaign) | 1,950,126 | 241 s | 0 | none |
| fuzz_decrypt_packet | same | 1,518,876 | 241 s | 0 | none |
| fuzz_transport | same | 2,124,863 | 241 s | 0 | none |
| fuzz_plain_unwrap | repo target, `BUILD_FUZZERS=ON`, clang++ 18 | 1,749,087 | 61 s | 0 | none |
| fuzz_decrypt_packet | same | 1,526,817 | 61 s | 0 | none |
| fuzz_transport_framing | same | 2,995,453 | 61 s | 0 | none |
| fuzz_tl_reader | repo target, `BUILD_FUZZERS=ON`, built 18:28, after its harness (14:32) and every library source were last modified | 1,697,293 | 61 s | 0 | none |

- The campaign exit codes come from `fuzz/summary.txt` (`EXIT=0` for each target, then `FUZZ4_DONE`).
- Campaign harnesses are the three `fuzz_*.cpp` files in `work/fuzz/`, compiled as instrumented copies in the scratch
  area. Their sources are identical to the repository files.
- Repo target smoke runs used empty starting corpora, so they show crash-freedom for 61 s only.
- No crash, leak, timeout or OOM artifact was written by any run.

Status: PASS for crash-freedom in these short runs. NOT RUN: long corpus runs (`tools/run_fuzz_long.sh`), the
`fuzz_session_string`, `fuzz_html_parser` and `fuzz_session_crypto` targets, and the malformed-padding-grammar sub-case.

## 4. Network drop and flood wait

Full table: `logs/network-drop-results.log`.

| Category | Status | Evidence |
|---|---|---|
| Offline simulated drop recovery (fake DC on loopback) | PASS | `ClientFakeDc.DropRecoveryReusesAuthKeyWithoutNewHandshake` passes. A drop reuses the auth key without a new handshake. |
| Offline simulated FLOOD_WAIT (no live rate limit) | PASS | `Session.FloodWait*` (6 tests), `Retry.FloodWait*` (2), `ClientFakeDc.StopCancelsAFloodWaitInProgress`. These are part of a 21-test targeted run (with CryptoAudit and DcMigration) that passes, exit 0. |
| Actual network interruption (packet-level loss or socket reset on a real path) | NOT RUN | No packet-level loss tool was used in this round. |
| Live drop against Telegram | NOT RUN | No live credentials were used. |
| Live FLOOD_WAIT | NOT OBSERVED | No live run was made. Nothing in this round provoked a server rate limit. |

## 5. Trailing-byte and padding investigation

- The 22- and 102-byte cases are covered by `CryptoAudit.ExactLiveTrailingCounts22And102EncryptedAreIgnored`, which
  passes on both compilers. Validation is not weakened: foreign key, bad msg_key and oversized length are still rejected
  (`CryptoAudit` tests above).
- Decision and evidence for plain (`auth_key_id = 0`) trailing bytes: `PROXY_AUDIT.md`, section 7.2.
- TDLib parity of this behaviour: BLOCKED. The upstream source could not be fetched through the allowed fetch path, so
  parity was not verified.

## 6. Tests not run

- Live Telegram tests (`cppgram_live_check`, `cppgram_live_tests`): NOT RUN, no credentials used.
- The public Cpplivetest bot's live run: NOT RUN.
- ARM64: NOT RUN.
