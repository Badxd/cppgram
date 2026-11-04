# CppGram: final offline report

Final commit: `4accf8e` on branch `production-readiness`.
Base for this round: `fa367fa` (previous package). Original base: `65daf2c` (origin/main at the start of the production-readiness work).

Scope: offline engineering only. Nothing here has been run against Telegram, a real proxy, or a real account. Live tests are
postponed to the project owner (see section 9). Nothing was pushed, no GitHub Actions run was started, and no VPS test was run.

Status labels used below: IMPLEMENTED, OFFLINE TESTED, GCC VERIFIED, CLANG VERIFIED, SANITIZER VERIFIED, FUZZ TESTED,
NOT VERIFIED LIVE, BLOCKED, UNSUPPORTED. Definitions are in `guides/PRODUCTION_READINESS.md`.

## 1. Project overview

CppGram is a C++20 MTProto client library for Telegram. It covers the transport (intermediate, padded intermediate, abridged,
full), the auth-key handshake, sessions (plain and encrypted), reconnect with backoff, FLOOD_WAIT handling, SOCKS5 and MTProto
obfuscated proxies, and the high-level bot and user API. This round focused on the encrypted session path, storage and client
concurrency, portability, and the framing and proxy tests.

## 2. Completed work in this round

| Phase | Work | Status |
|---|---|---|
| 1. CEG1 fuzzing | Harness split into header check, authenticated open and sampled full decrypt. Per-salt key cache. Valid and invalid seeds. Coverage notes. | IMPLEMENTED, FUZZ TESTED |
| 2. Bot and client lifecycle | Five deterministic lifecycle tests: connect/disconnect cycles, stop before connect, concurrent stop, state reads during reconnects, no handler after destruction. Found and fixed the `run()`/`stop()` ordering race. | IMPLEMENTED, OFFLINE TESTED, SANITIZER VERIFIED |
| 3. Session storage concurrency | Mutex on `SqliteStorage`. Six tests: no torn reads, two stores on one file, repeated open/close, corrupted and truncated files, no key material in errors. Handle leak and NULL-text fixes. | IMPLEMENTED, OFFLINE TESTED, SANITIZER VERIFIED |
| 4. Portability | Fresh build directories for g++-12 and clang++-18. Removed false-positive GCC 12 warnings. Removed unused Clang constants. | GCC VERIFIED, CLANG VERIFIED |
| 5. Padded-intermediate framing | Five tests: many frames in one read, split at every byte, truncated frames, server error words, padding overrun. Decrypt now ignores up to 15 trailing bytes after the ciphertext blocks. | IMPLEMENTED, OFFLINE TESTED, NOT VERIFIED LIVE |
| 6. Security audit | Targeted review of the listed areas. Eight findings fixed with tests (section 7). Full review is in `guides/SECURITY_AUDIT.md`. | IMPLEMENTED, OFFLINE TESTED |
| 7. Proxy support | SOCKS5 and obfuscated proxy formats documented with their limits. Fake-TLS secrets rejected before any connection. | IMPLEMENTED, OFFLINE TESTED, UNSUPPORTED (fake TLS) |
| 8. Offline suite | Full runs listed in section 4. | GCC VERIFIED, CLANG VERIFIED, SANITIZER VERIFIED, FUZZ TESTED |
| 9. Extended fuzzing | Four targets, 600 s each, plus seeded runs. Numbers in section 5. | FUZZ TESTED |
| 10. Documentation | README, CHANGELOG, production readiness, Linux install (GCC and Clang), fuzz README, session encryption, proxies, reconnect and FLOOD_WAIT, security audit. | IMPLEMENTED |
| 11. Final review | Commit-by-commit review, warning cleanup, removal of unused code. | IMPLEMENTED |
| 12. Package | This ZIP. | see section 10 |

Limits on what was done: the bot-login path is not covered by the fake DC. Encrypted-session operations are tested for
correctness and at the storage level, but there is no client-level concurrency test for them.

## 3. Files changed in this round

Commits since `fa367fa` (`git diff --stat fa367fa HEAD`: 22 files, 879 insertions, 118 deletions):

- `src/session_crypto.cpp`, `src/session_crypto_detail.h` (new): scrypt bound, size cap, header and open seam, key derivation seam.
- `src/storage.cpp`, `include/cppgram/storage.h`: mutex, `open_store()`, handle close on failed open, NULL check.
- `src/client.cpp`, `include/cppgram/client.h`: `run()` start under the stop lock, `is_running()`, handler-registration contract, unused constants removed.
- `src/client_extra.cpp`: GCC 12 false-positive warning fixed (same values).
- `src/mtproto/crypto.cpp` (earlier in this round): trailing transport padding accepted.
- `fuzz/fuzz_session_crypto.cpp`, `fuzz/make_session_seeds.cpp`, `fuzz/README.md`: CEG1 harness, valid and invalid seeds, documentation.
- `tests/CMakeLists.txt`, `tests/mtproto/session_crypto_test.cpp`, `tests/mtproto/obfuscated_test.cpp`,
  `tests/mtproto/client_fake_dc_test.cpp`, `tests/mtproto/fake_dc.h`, `tests/mtproto/storage_concurrency_test.cpp` (new).
- `guides/PRODUCTION_READINESS.md`, `guides/SECURITY_AUDIT.md` (new), `guides/SESSION_ENCRYPTION.md`, `guides/LINUX_INSTALL.md`,
  `README.md`, `CHANGELOG.md`.

Known history quirk: commit `71c8a69` references `storage_concurrency_test.cpp` in `tests/CMakeLists.txt`, which is only added in
`fa03935`. Build from `fa03935` or later.

## 4. Build and test results

| Item | Result |
|---|---|
| GCC version | g++-12 12.4.0 (Ubuntu 12.4.0-2ubuntu1~24.04.1) |
| GCC build | Fresh directory `build/gcc-clean`, Release, tests on. Success. Two GCC 12 false-positive warnings found in `client_extra.cpp` and fixed. Rebuild after fix: 0 warnings. |
| GCC unit tests | 339 of 339 passed (ctest, on the final source) |
| Clang version | clang++-18 18.1.3 (Ubuntu 18.1.3-1ubuntu1) |
| Clang build | Configured fresh in `build/clang-clean`. Note: an interrupted first attempt in the same directory was resumed, so some objects came from it. The full target set (library, tests, examples) built with exit 0 and 0 warnings. |
| Clang unit tests | 339 of 339 passed (ctest) |
| Lifecycle stress | 16 tests (`ClientLifecycle.*` 5, `ClientFakeDc.*` 11) passed, repeated three times |
| Session-storage concurrency | 6 tests (`StorageConcurrency.*`) passed |
| SOCKS5 offline | `Socks5.*`, `Socks5Connection.*` passed |
| Reconnect offline | `ClientFakeDc.*` passed |
| FLOOD_WAIT offline | `ClientFakeDc.FloodWait*`, `ClientFakeDc.Consecutive*`, `ClientFakeDc.StopCancels*` passed |
| Encrypted session offline | `SessionCrypto.*`, `SessionStringEncrypted.*`, `EncryptedSession.*`, `SqliteEncrypted.*` passed |
| MTProto obfuscated proxy offline | `Obfuscated*` passed |
| Padded-intermediate framing offline | `ObfuscatedFrame.*` passed |
| Header self-containment | `tools/check_headers.sh`: exit 0 |
| Example compilation | `tools/check_examples.sh`: exit 0 |
| BLOCKED tests | None among the offline tests. Live tests are postponed by request (section 9). |

## 5. Sanitizer and fuzz results

ASan and UBSan (`tools/run_sanitizers.sh`, `-O0 -g0`, `-j1`, `build-san/`, final source): 339 of 339 tests passed,
no AddressSanitizer, LeakSanitizer or UBSan reports, exit 0. SANITIZER VERIFIED.

TSan (`tools/run_tsan.sh`, `-O1 -g0`, `-j1`, `build-tsan/`, final source): focused run 35 tests passed, full binary
260 tests passed, 0 ThreadSanitizer warnings, exit 0. SANITIZER VERIFIED.

Fuzzing (clang-18 libFuzzer with ASan, every run exited 0, no crash, timeout or sanitizer report):

| Target | Corpus | Duration | Executions | Coverage (edges) |
|---|---|---|---|---|
| `fuzz_tl_reader` | none | 601 s | 16,749,453 | 30 |
| `fuzz_session_string` | none | 601 s | 446,703,929 | 13 |
| `fuzz_session_string` | 8 valid | 301 s | 106,432,734 | 14 |
| `fuzz_html_parser` | none | 601 s | 10,908,327 | 28 |
| `fuzz_session_crypto` | none | 601 s | 15,812,122 | 5 |
| `fuzz_session_crypto` | 4 valid + 11 invalid CEG1 | 601 s | 21,385 | 16 |

Regression findings from fuzzing: none. No crash occurred, so no crash regression input was added. The CEG1 execution counts are low
because each decryption that passes the header runs scrypt (about 30 ms). The TL reader has no seed corpus in the repository.

## 6. Coverage information

- TL reader, session string and HTML parser: libFuzzer edge counts above. These show that the runs reached new code, but they
  are not a full coverage measurement.
- CEG1: header validation, the authenticated open and the sampled full decrypt are all reached. Coverage is limited to one
  passphrase and to the code reached through the seeds.
- Unit tests: 339 cases. Not measured as line or branch coverage. No coverage tool was run.

## 7. Security fixes

1. Encrypted session: scrypt cost accepted up to 2^20 and r = 16 (about 2 GiB) from an untrusted file. Bound lowered to 2^17
   and r = 8 (at most 128 MiB). Writer unchanged.
2. Encrypted session: no input size cap, and `size_t` to `int` casts. Blobs above 16 MiB refused before any work.
3. SQLite store: handle leaked when the constructor threw after `sqlite3_open`. Fixed.
4. SQLite store: NULL result of `sqlite3_column_text` used to build a string. Checked.
5. SQLite store: one handle shared without a lock. Each method now takes a mutex.
6. Client: `run()` set `running_` outside the lock that installs the stop source, so a `stop()` could be lost. Fixed.
7. Client: `~Client` ran user handlers during destruction. Handlers are cleared first.
8. Padded transport: valid frames with 0 to 15 bytes of padding were rejected. Trailing bytes are ignored; msg_key checking unchanged.

## 8. Known limitations

- No live verification of anything that touches Telegram.
- Obfuscated proxy: fake-TLS (`ee`) secrets are UNSUPPORTED. Whether real servers pad their frames is NOT VERIFIED LIVE.
- Handlers (`Client::on_*`) must be registered before `connect()` or `run()`. Registration during a run is a data race.
- Bot login is not exercised by the offline fake DC.
- Scrypt allocates up to 128 MiB for a maximum-cost encrypted session.
- Fuzzing runs are 10 minutes at most per target. No long campaign.
- One machine. No CI run for this snapshot.
- `Client::stop()` before `run()` is ignored by design.

## 9. Live tests postponed for the project owner

- NOT VERIFIED LIVE — USER WILL TEST LATER: real Telegram reconnect.
- NOT VERIFIED LIVE — USER WILL TEST LATER: real Telegram FLOOD_WAIT.
- NOT VERIFIED LIVE — USER WILL TEST LATER: SOCKS5 connection to Telegram.
- NOT VERIFIED LIVE — USER WILL TEST LATER: encrypted session restore with a real Telegram account.
- NOT VERIFIED LIVE — USER WILL TEST LATER: real MTProto obfuscated proxy compatibility.
- NOT VERIFIED LIVE — USER WILL TEST LATER: real Telegram padded-intermediate server framing compatibility.

## 10. Unsupported

- Fake-TLS (`ee`) proxy secrets: UNSUPPORTED. Rejected before any connection. Test: `ObfuscatedNet.FakeTlsSecretIsRejectedBeforeConnecting`.

## 11. Commit history

See `commit-history.txt` in this package.

## 12. Package

ZIP: `cppgram-final-offline_4accf8e.zip`. Its SHA256 is printed with the package and written to a `.sha256` file beside it.
The ZIP is a `git archive` of commit `4accf8e`, plus this report, `commit-history.txt`, and generated fuzz seeds under `fuzz/corpus/`.
Those seeds hold random test keys and the fixed test passphrase `fuzz-passphrase`. They are not from any account.
No build directories, session files, databases, API credentials, tokens, passphrases or private keys are included.
