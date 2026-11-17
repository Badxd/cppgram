> **Historical.** This file was written for an earlier pre-release round (0.1.0). It is kept for the record. The current status is in `README.md`, `KNOWN_LIMITATIONS.md` and `FINAL_V1.0.0_READINESS_REPORT.md`.

# Final report: CppGram 0.1.0, release verification (baseline 1cd09d0)

This report replaces the earlier release report. Every status below comes from a command run in this round, and each
item names its evidence. Log paths are relative to the evidence package root. Hashes are in `SHA256SUMS.txt`.

Status values: PASS, FAIL, BLOCKED, NOT OBSERVED, NOT RUN, PENDING USER APPROVAL.

## 1. Summary

- Clean GCC 13.3.0 and Clang 18.1.3 Release C++20 builds: both PASS, zero warnings, exit 0.
- Full CTest on both compilers: 711/711 PASS.
- Per-binary totals on both compilers: mtproto 457, async 46, bot 110, media 22, metrics 23, security 53. All PASS.
- Targeted sanitizer runs: ASan+UBSan 118/118 PASS, TSan 47/47 PASS (mtproto binary subsets only).
- Fuzzing: short runs on the plain-unwrap, decrypt-packet, transport and TL-reader targets, with no crash, leak, timeout or
  OOM artifact. Long runs NOT RUN.
- Network drop and flood wait: offline simulated only. PASS. Packet-level and live drops NOT RUN. Live FLOOD_WAIT NOT OBSERVED.
- Install and `find_package` consumer PASS. Quality gate (`make checks`) PASS.
- Public Cpplivetest bot builds against the current library and passes its unit test. Its live run NOT RUN.
- No Telegram live test was run. No credential was used.
- Repository: no commits, tags, branches or pushes. HEAD is 1cd09d0 before and after.

Release readiness: the library and its offline test suite are verified on Linux x86_64 with GCC 13.3 and Clang 18.1.
This is not a production-readiness claim. Live Telegram behaviour, ARM64 and packet-level network interruption are unverified,
and two owner decisions are open (section 3).

## 2. Results by item

### Build and offline tests

| Item | Status | Evidence |
|---|---|---|
| GCC 13.3.0 Release C++20 clean build | PASS | `logs/gcc-build.log` (BUILD=0), `logs/gcc-configure.log` (CFG=0) |
| Clang 18.1.3 Release C++20 clean build | PASS | `logs/clang-build.log` (BUILD=0), `logs/clang-configure.log` (CFG=0) |
| Zero compiler warnings, both builds | PASS | `grep -c 'warning:'` on both build logs: 0 |
| CTest, GCC (711 cases) | PASS | `logs/ctest-gcc.log` (CTEST=0, 711/711) |
| CTest, Clang (711 cases) | PASS | `logs/ctest-clang.log` (CTEST=0, 711/711) |
| Per-binary totals, GCC: 457/46/110/22/23/53 | PASS | `logs/cppgram_*_test-clean-gcc.log`, each exit 0 |
| Per-binary totals, Clang: 457/46/110/22/23/53 | PASS | `logs/cppgram_*_test-clean-clang.log`, each exit 0 |
| Targeted ASan+UBSan (mtproto subset, 118 cases) | PASS | `logs/sanitizers/asan-ubsan.log`; instrumentation confirmed by symbols in the binary |
| Targeted TSan (mtproto subset, 47 cases) | PASS | `logs/sanitizers/tsan.log`; 0 ThreadSanitizer warnings |
| Full ASan, TSan suites; other binaries under sanitizers | NOT RUN | Only the subsets above were built and run. |

### Fuzzing

| Item | Status | Evidence |
|---|---|---|
| fuzz_plain_unwrap, fuzz_decrypt_packet, fuzz_transport (instrumented, 241 s each, about 1.5–2.1 M executions each) | PASS | `logs/fuzz/fuzz_*.log`, `logs/fuzz/summary.txt` (EXIT=0 each), `logs/fuzz-results.log` |
| Repo targets fuzz_plain_unwrap, fuzz_decrypt_packet, fuzz_transport_framing (61 s each) | PASS | `logs/fuzz/repo-smoke2-*.log` (exit 0) |
| fuzz_tl_reader (61 s, about 1.7 M executions) | PASS | `logs/bot-and-tools/fuzz-tl-reader-smoke.log` |
| Long corpus runs (`tools/run_fuzz_long.sh`) | NOT RUN | — |
| fuzz_session_string, fuzz_html_parser, fuzz_session_crypto | NOT RUN | — |
| Malformed-padding-grammar sub-case | NOT RUN | Documented gap, see `KNOWN_LIMITATIONS.md`. |

### Network drop, flood wait and padding

| Item | Status | Evidence |
|---|---|---|
| Offline simulated drop recovery (reuses auth key, no new handshake) | PASS | `logs/network-drop-offline-simulated.log`, `logs/network-drop-results.log` |
| Offline FLOOD_WAIT: sleep, cancel, deadline, bounded retry, strict format | PASS | same logs (6 Session + 2 Retry + 1 ClientFakeDc tests) |
| Packet-level or socket-reset network interruption | NOT RUN | No such tool was used. |
| Live drop against Telegram | NOT RUN | No credentials used. |
| Live FLOOD_WAIT | NOT OBSERVED | No live run. |
| 22- and 102-byte trailing-byte cases, exact | PASS | `CryptoAudit.ExactLiveTrailingCounts22And102EncryptedAreIgnored` in `logs/cppgram_mtproto_test-clean-*.log` |
| Validation not weakened (foreign key, bad msg_key, oversized length) | PASS | `CryptoAudit.*` in the same logs |
| Plain trailing-byte behaviour, TDLib parity | BLOCKED | TDLib source could not be fetched. `PROXY_AUDIT.md` section 7.2 records the decision. |

### Live Telegram and public bot

| Item | Status | Evidence |
|---|---|---|
| Live Telegram tests (six cases) | NOT RUN | No credentials used. |
| Live-offline wiring (no Telegram contact) | PASS | `logs/bot-and-tools/make-live-offline-check.log`, exit 0 |
| Public Cpplivetest bot builds against current library, unit test 1/1 | PASS | `logs/bot-and-tools/bot-compat-public-*.log` |
| Public Cpplivetest bot live run | NOT RUN | No credentials used. |
| Public Cpplivetest committed binary | PENDING USER APPROVAL | `cpplivetest-review.md` (owner action: stop tracking, rotate if needed) |
| Local bot patch applies to b8db65c | PASS | `git apply --check` against a fresh clone of the public repo. Not pushed. |

### Build, install and release

| Item | Status | Evidence |
|---|---|---|
| `make checks` (source, version, format, api, examples) | PASS | `logs/bot-and-tools/make-checks.log` (MAKE_CHECKS=0) |
| API index (476 Client members) | PASS | `logs/bot-and-tools/make-api-index-check.log` |
| Version and changelog | PASS | `logs/bot-and-tools/check-version.log` |
| Install and consumer `find_package` | PASS | `logs/bot-and-tools/install.log`, `consumer-*.log` |
| Release source archive, build and verify | PASS | `source/cppgram-0.1.0-source.zip` (`make package` and `make verify-release`) |
| ARM64 | NOT RUN | — |
| Windows | BLOCKED | Not a supported platform. Not attempted. |
| actionlint, Doxygen, schema regeneration | NOT RUN | `BUILD_RESULTS.md` |

### Security review

| Item | Status | Evidence |
|---|---|---|
| SR-1, SR-2: flood-wait cancel and deadline defects | PASS (fixed) | `security-review.md` |
| SR-3: unchecked `atoi` on server text | PASS (fixed) | `src/dc.cpp`, `dc_migration_fake_dc_test.cpp` |
| SR-4: unbounded length allocation | PASS | `security-review.md` |
| SR-5: VPS and live-credential rotation | PENDING USER APPROVAL | Not done. |
| SR-6: telemt public 8443 | PENDING USER APPROVAL | Not changed. |
| SR-7: committed binary in public bot repo | PENDING USER APPROVAL | `cpplivetest-review.md` |
| SR-8: public bot docs | PENDING USER APPROVAL | Local patch only. |
| SR-9 to SR-12: webapp, logging, repo hygiene, fake DC | PASS | Code review and scans; see `security-review.md` |
| Secret scan of the release package (including binaries and logs) | PASS | `logs/bot-and-tools/secret-scan-package.txt`, see section 4 |

### Repository state

| Item | Status | Evidence |
|---|---|---|
| No commits, tags, branches or pushes | PASS | `commit-history.txt` (HEAD 1cd09d0 before and after) |
| Changed files against baseline | PASS | `changed-files.txt`: 14 added, 21 modified, 0 deleted (includes `commit-history.txt` and `SHA256SUMS.txt`, which replaced baseline versions) |
| Build outputs kept out of the source tree | PASS | Build directories were moved to the scratch area. |

## 3. Open decisions (PENDING USER APPROVAL)

1. Rotate the VPS and live-test credentials (SR-5). Not done.
2. Decide whether telemt stays on public 8443, classic and secure modes (SR-6). Not changed.
3. Stop tracking `bin/cppgram_live_test_bot` in the public Cpplivetest repository (SR-7). Owner action; see `cpplivetest-review.md`.

## 4. Package and checks

- Evidence package: this folder and `SHA256SUMS.txt` (SHA-256 for every file in the package except `SHA256SUMS.txt`
  itself, which cannot record its own hash).
- The release source archive inside `source/` carries its own `SHA256SUMS.txt` and passed `verify-release`.
- The secret scan covers every file in the package, including binaries and logs. Its results are in
  `logs/bot-and-tools/secret-scan-package.txt`. It prints only file, line and pattern name, never a matched value.

## 5. Changes from earlier figures

- Earlier checkpoint: 700/700 CTest and 446/446 MTProto. Current: 711/711 CTest and 457 MTProto (457 on each compiler).
- The pre-mission tree had 702 test macros (448 MTProto). Two of those were added after the earlier checkpoint was accepted.
  The preserved artifacts do not show which two, so this is recorded as a limitation, not inferred.
- This round added 9 MTProto tests: six CryptoAudit tests, plus one each for drop recovery, the bounded flood-wait retry
  loop and the flood-wait digit boundary. No test was removed.
- Full reconciliation: `TEST_RESULTS.md`, section 1.
