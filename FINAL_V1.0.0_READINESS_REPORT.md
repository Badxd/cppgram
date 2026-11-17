# CppGram 1.0.0: final readiness report

Status: **NOT READY**. This is not a stable release. The local verification is complete, but live Telegram validation
has not run and several owner decisions are open. Do not announce 1.0.0 as stable until the blockers below are closed.

This is the in-tree copy. It has the verification results. The archive hashes and the post-extraction results are in the
top-level copy shipped with the archives, because a file cannot contain the hash of the archive it sits in. Results
and logs are in `reports/final-1.0.0/`; the stage records and exit codes are in `reports/final-1.0.0/MANIFEST.md`.

Status values: PASS, FAIL, BLOCKED, NOT RUN, PENDING USER APPROVAL.

## Decision

- Local verification: PASS on the final source tree. 711 of 711 tests on GCC 13.3 and Clang 18.1.3. ASan and UBSan on the
  full suite. TSan on all six test binaries. Seven libFuzzer targets, 120 s each, no findings. Install and an independent
  consumer. Schema check, project checks and static analysis are run, with their results recorded.
- Live Telegram: BLOCKED. No live Telegram validation has occurred in this release. On the sandbox network every live
  check is blocked by the interception gate or by missing credentials, and every live check reports `telegram_contact=no`.
  No live result exists for this release.
- Release status: NOT READY, until the blockers below are closed.

## Blockers

| ID | Item | Blocks publication | Owner action |
|---|---|---|---|
| RB-1 | Live Telegram validation on an unrestricted network with owner credentials. Scope, from the blocker records: direct handshakes (intermediate, abridged, full), encrypted ping, reconnect with saved key, lifecycle, bot login, session restore, dd proxy handshake. Message sending (T-8) and controlled FLOOD_WAIT (T-9) are tracked as separate live items. | Yes | Run `CppGramLiveTest` on an unrestricted network with owner credentials. Resolved only when real PASS or FAIL results from that network are recorded in `CppGramLiveTest/TEST_RESULTS.md`. Status: **unresolved**. The owner commands are in the top-level copy and `CppGramLiveTest/LIVE_TESTING.md`. |
| RB-2 | Rotation of development credentials used on the VPS and in the live-test project | Yes, for publication | Rotate, then confirm. This build did not rotate anything. |
| RB-3 | Public MTProxy on port 8443 (classic and secure modes) | Yes, until decided | Decide whether to keep, restrict or stop it. This build did not touch it. |
| RB-4 | Binary `bin/cppgram_live_test_bot` committed in the external `Badxd/Cpplivetest` repository (10.8 MB) | No, for the library | Decide whether to remove it from that repository. This build did not change that repository. |

## Owner decisions applied in this round

- The live-test repository `https://github.com/Badxd/Cpplivetest` is kept and labelled as the separate CppGramLiveTest
  project. No Badmunda05 live-test URL is invented.
- The nine upstream tgbot-cpp Dockerfiles and the `echobot-submodule` example are not CppGram examples. They are kept,
  unchanged, under `reference/tgbot-cpp-upstream/` and labelled as third-party reference material.
- The tgbot-cpp MIT attribution is kept in `bot.h`, `NOTICE` and `LICENSE`.

## Requirement status

### Library

| ID | Requirement | Status | Evidence | Note |
|---|---|---|---|---|
| L-1 | Reconcile the attached snapshot with the round-4 fixes, without losing attached code | PASS | Earlier diff review; public headers differ only in `app_version`. | |
| L-2 | Auth and crypto regression tests (plain padding, encrypted trailing bytes) | PASS | `Crypto.*`, `Handshake.PlainPacketIgnoresTrailingPaddingOfAnySize`. | |
| L-3 | Full suite on GCC 13.3 | PASS | 711 of 711, 0 warnings. `logs/s0-ctest.log`, `s0-warnings.txt`. | |
| L-4 | Full suite on Clang 18.1.3 | PASS | 711 of 711, 0 warnings. `logs/s1-ctest.log`. | |
| L-5 | MTProto transport, framing, encryption | PASS (offline) | 457 MTProto tests under TSan and ASan. Fuzzing: `TEST_RESULTS.md` section 4. | Live validation: RB-1 |
| L-6 | Auth, session persistence and restoration | PASS (offline) | `offline.session_roundtrip` (live: RB-1). | |
| L-7 | Reconnect with saved key | PASS (fake DC) | `ClientFakeDc.DropRecoveryReusesAuthKeyWithoutNewHandshake`. | Live: RB-1 |
| L-8 | Proxy plain and dd secrets | PASS (offline) | `ProxyStub.*` and proxy tests. | Live proxy: RB-1 and owner approval |
| L-9 | FLOOD_WAIT propagation and cancel | PASS (fake DC) | `Session.FloodWait*`, `Retry.FloodWait*`, `ClientFakeDc.StopCancelsAFloodWaitInProgress`. | Controlled live FLOOD_WAIT: NOT RUN |
| L-10 | API consistency | PASS | `api_index.py --check`: 476 Client members, all defined. | |
| L-11 | Memory safety | PASS | ASan and UBSan full suite: 711 of 711, 0 reports, 116.24 s. TSan on all six test binaries: 711 tests, 0 reports. | MSan not run. |
| L-12 | Fuzzing | PASS (bounded) | Seven targets, 120 s each, instrumented library: 36,962,006 executions, 0 findings. | Bounded only. Long campaigns not run. |
| L-13 | CMake install and exported package | PASS | `cmake --install` exit 0, package `CppGram` version 1.0.0 under `lib/cmake/CppGram`. | |
| L-14 | Independent consumer | PASS | `find_package(CppGram 1.0.0 EXACT)`, built with `-Werror`, run with GCC and Clang. | |
| L-15 | Schema check (`make schema-check`) | PASS | Exit 0. The 19 generated files match the generator. `schema/SHA256SUMS.txt` refreshed. | Generated files regenerated: naming-only change, see CHANGELOG. |
| L-16 | Project checks (`make checks`) | PASS | Exit 0 before and after the documentation rewrite. 21 OK lines. | |
| L-17 | Formatting (clang-format 18.1.3) | PASS | 56 files, 0 failing. | |
| L-18 | Static analysis (clang-tidy-18) | PASS (no confirmed defect) | 304 warnings in `bugprone-*`, `clang-analyzer-*`, `performance-*`. Triage of 12 `bugprone` findings: no confirmed defect. | 304 warnings not fixed. `logs/s5-tidy-raw.log`. |
| L-19 | Documentation build (Doxygen 1.9.8) | PASS (builds) | Exit 0. | 899 warnings, mostly markup in comments. Not fixed. |
| L-20 | Live Telegram validation of the library | BLOCKED | Every live check blocked by the interception gate or missing credentials. | RB-1 |
| L-21 | ARM64 build and tests | NOT RUN | No ARM64 hardware. | Platform claim scoped to Linux x86_64. |
| L-22 | TDLib parity of trailing-byte handling | BLOCKED | Upstream source not reachable through the allowed fetch path. | Not claimed. |
| L-23 | Unsupported features documented | PASS | Fake-TLS, secret chats, calls and Windows: `KNOWN_LIMITATIONS.md`. | |
| L-24 | Version consistency | PASS | VERSION, `client.h`, CHANGELOG all 1.0.0. | |
| L-25 | Public headers | PASS | Only change against the snapshot: `app_version` 0.1.0 to 1.0.0. No removals. | |
| L-26 | Documentation | PASS | README, CHANGELOG, RELEASE_NOTES, KNOWN_LIMITATIONS, TEST_RESULTS, security-review rewritten from verified results. | Historical reports carry a HISTORICAL banner. |
| L-27 | Examples | PASS | 16 example files parse against the public headers. Upstream material moved to `reference/`. | |
| L-28 | No push, commit, tag, branch, release or package publication | PASS | No git operations and no publication in this run. | |

### Live-test project (`CppGramLiveTest`)

| ID | Requirement | Status | Evidence | Note |
|---|---|---|---|---|
| T-1 | Runner with categories offline, environment, live_no_credentials, live_credentials, owner_approval | PASS | `live/livetest-default-mode.log`. | |
| T-2 | Statuses PASS, FAIL, BLOCKED, NOT RUN, PENDING USER APPROVAL | PASS | Five statuses only. | |
| T-3 | Interception gate, with and without `CGLT_NETWORK_UNRESTRICTED=1`, covers `live_no_credentials`, `live_credentials`, `owner_approval` and the bot at startup. No live result from an intercepted network. | PASS | Gate-fix runs in `CppGramLiveTest/TEST_RESULTS.md` section 7: credential, owner and no-credential checks BLOCKED with the override, `telegram_contact=no`; bot refused with exit 3; `connect()` only to the probe address. | Corrected in this round. Earlier T-3 tested only `live_no_credentials`. |
| T-4 | Owner-gated checks: network gate first, then `CGLT_OWNER_APPROVED=1` | PASS | Default run, no environment: 4 PASS, 12 BLOCKED, 0 PENDING on the intercepted sandbox network. Owner checks are PENDING only on a clean, unrestricted network without approval (gate tests). | Corrected in this round. The earlier "3 PENDING" predates the network gate covering owner tests. |
| T-13 | Gate regression tests: intercepted with credentials, intercepted with override, unrestricted without approval, unrestricted with approval, credentials not read before the gate | PASS | `livetest_gate_tests`: 63 checks, 0 failures (`CppGramLiveTest/reports/logs/gate-fix/`). | Offline tests of the gate logic. A live run on an unrestricted network: not run. |
| T-14 | Unknown category, unknown test and unwritable `--json` are usage errors (exit 2) | PASS | `g-typo-category.out`, `h-typo-test.out`, `i-bad-json-path.out` in `CppGramLiveTest/reports/logs/gate-fix/`. | |
| T-15 | Bot refuses startup unless the probe is clean and `CGLT_NETWORK_UNRESTRICTED=1` is set (exit 3) | PASS | `f-bot-startup.out`: refused with exit 3 on the intercepted network. | Verified on the sandbox network only. |
| T-5 | Live handshakes, encrypted ping, reconnect, lifecycle | BLOCKED | Intercepted network. | RB-1 |
| T-6 | Bot login, `get_me`, session restore | BLOCKED | No credentials, intercepted network. | RB-1 |
| T-7 | dd proxy | PENDING USER APPROVAL | Needs owner proxy, secret and approval. | Owner action |
| T-8 | Message sending | PENDING USER APPROVAL | Not implemented in this build. | Owner decision |
| T-9 | Controlled FLOOD_WAIT | NOT RUN | No safe trigger. | |
| T-10 | Bot unit tests | PASS | `live/livebot-unit.log`: 136 checks, 0 failures. | |
| T-11 | Library digest reported | PASS | `57e313ea306c76c3f2de6cc97e6275b9e6664c1f53f3c4fb53daf409037ad58d` in the runner header and JSON. | |
| T-12 | Secrets in the project | PASS | Scan: no real credential. Only dummy `123456789:AAExample…` fixtures in test files. | |

### Packaging

| ID | Requirement | Status | Evidence | Note |
|---|---|---|---|---|
| P-1 | ZIP and TAR.GZ from one inventory and one revision | See the top-level copy | | Packaging runs after this in-tree copy is frozen. |
| P-2 | Independent extraction and verification of each archive | See the top-level copy | | |
| P-3 | Live-test archive rebuilt against the exact final source and verified | See the top-level copy | | |
| P-4 | Stale archives in `final/deliver/` are not used as final | PASS | They are kept under a stale name and are not listed as deliverables. | |

## What would change the decision

Release status changes to READY only when all of these are true:

1. RB-1: live Telegram validation runs on an unrestricted network with owner credentials, and the results are recorded in
   `CppGramLiveTest/TEST_RESULTS.md` with real PASS or FAIL statuses.
2. RB-2: development credentials are rotated and the rotation is confirmed.
3. RB-3: the public 8443 proxy has a decision.
4. RB-4: the owner decides on the binary in the external live-test repository.
5. The owner explicitly approves publication.
