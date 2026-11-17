# Known limitations: CppGram 1.0.0 (release candidate)

Status values: PASS, FAIL, BLOCKED, NOT RUN, PENDING USER APPROVAL. An item is PASS only when it was run in this release,
and `TEST_RESULTS.md` has the command, exit code and count.

## Release blockers (critical)

| ID | Item | Status | Why it blocks a stable release | Owner action |
|---|---|---|---|---|
| RB-1 | Live Telegram validation: bot login, session restore, direct handshake, encrypted ping, reconnect, lifecycle, dd-proxy handshake | BLOCKED | Every live result in this release is BLOCKED. The development sandbox intercepts connections to Telegram addresses, so no live result from it is valid. Stable status requires real results. | Run `cppgram_livetest` from `CppGramLiveTest` on an unrestricted network with `CGLT_NETWORK_UNRESTRICTED=1`, and bot credentials for the account tests. See `CppGramLiveTest/TESTS.md`. |
| RB-2 | Rotation of the VPS and live-test credentials | PENDING USER APPROVAL | Operational security. Credentials used during development must be treated as exposed. | Owner decides and rotates. Nothing was rotated in this release. |
| RB-3 | Public proxy port (telemt 8443, classic and secure modes) | PENDING USER APPROVAL | Infrastructure exposure, not library behaviour. | Owner decides whether to keep it public. Not changed. |

RB-2 and RB-3 are infrastructure decisions. They do not change the library code, but a stable release should not be
published while they are open.

## Live Telegram behaviour

- Dd-proxy padding was observed in earlier live captures at 22, 96, 102, 108, 116 and 123 trailing bytes after the
  plain-reply length field. That evidence comes from earlier rounds. It is not re-verified in this release.
- The encrypted path tries at most three extra 16-byte blocks beyond the minimal padded length when it locates a message
  (`src/mtproto/crypto.cpp`). A server message whose inner padding is longer than that would be rejected with
  `CryptoError`. The MTProto specification allows inner padding up to 1024 bytes. Telegram's servers were not observed
  to use more than the minimum padding, but this is not verified live.
- FLOOD_WAIT: no live FLOOD_WAIT has been observed. Handling is verified only against the fake DC. There is no safe
  controlled trigger on a live account.
- Drop recovery is verified only against the fake DC (a dropped connection reuses the key with no second handshake).
  Real network interruptions have not been tested.

## Protocol scope

- Fake-TLS (`ee`) MTProxy secrets are refused by design (`ProxySecretKind::FakeTls`). Plain and dd (padded
  intermediate) secrets are supported.
- Secret chats and calls are not implemented.
- TDLib parity of the plain trailing-byte handling: BLOCKED. The upstream source could not be fetched through the
  allowed path. The decision in `PROXY_AUDIT.md` section 7 rests on the specification and on CppGram's own tests.
- Behavioural parity with Kurigram or other libraries is not claimed. `PARITY_MATRIX.md` classifies evidence only.

## Fuzzing

- Bounded runs: 120 s for each of the seven libFuzzer targets, on the final tree. 36,962,006 executions in total, no
  findings (`TEST_RESULTS.md`, section 4). Crash-freedom in these runs does not prove the absence of defects.
- The library is built with `-fsanitize=fuzzer-no-link`, so its code is coverage-instrumented. The coverage values
  libFuzzer prints are counts of covered program counters, not percentages. No total was measured, so coverage is not
  quantified.
- `fuzz_session_crypto` ran only 7,184 executions in 120 s, because each input does authenticated key derivation.
- Memory growth in earlier logs came from ASan's freed-memory quarantine. The release runs use
  `ASAN_OPTIONS=quarantine_size_mb=1`, and peak RSS was 50 to 83 MB.
- Long campaigns (`tools/run_fuzz_long.sh` with its default budgets) were not run in this release.

## Sanitizers

- ASan and UBSan: the full suite, 711 of 711 with no sanitizer reports, Clang 18.1.3, `-O0`. A first parallel build
  attempt failed on one object with no diagnostic. A standalone recompile succeeded and the serial rebuild passed
  (`TEST_RESULTS.md`, section 3).
- TSan: all six test binaries, 711 tests, no reports, Clang 18.1.3.
- MSan: not run.

## Platforms and build

- Verified: Linux x86_64, GCC 13.3.0 and Clang 18.1.3, Release and C++20. Linux kernel 6.18.44, Intel Xeon 2.10 GHz, two
  cores.
- NOT RUN: ARM64. Hardware was not available.
- Unsupported: Windows.
- The build does not use `-Werror`. Both compilers reported zero warnings in the full builds (`TEST_RESULTS.md`).
- Static analysis: clang-tidy-18 reports 304 warnings, mostly style-level `performance-unnecessary-value-param`. The 12
  `bugprone` findings were triaged with no confirmed defect. The warnings are not fixed in this release.
- Documentation: Doxygen 1.9.8 builds with 899 warnings, mostly markup in comments. They are not fixed in this release.

## Platforms and build

- Verified: Linux x86_64, GCC 13.3.0 and Clang 18.1.3, Release and C++20. Linux kernel 6.18.44, Intel Xeon 2.10 GHz, two
  cores.
- NOT RUN: ARM64. Hardware was not available.
- Unsupported: Windows.
- The build does not use `-Werror`. Both compilers reported zero warnings in the full builds (`TEST_RESULTS.md`).

## Packaging and history

- This archive is not a git checkout. `commit-history.txt` is a change record written for this release, not git history.
  No commits, tags or branches were created, and nothing was pushed.
- `FINAL_REPORT.md`, `BUILD_RESULTS.md`, `TEST_RESULTS.md` history sections and `PROXY_AUDIT.md` describe the earlier
  0.1.0 rounds. They are kept for the record. Where they disagree with this file, this file and
  `FINAL_V1.0.0_READINESS_REPORT.md` are current.
