# Audit status

Each entry says what was checked, the evidence, and what is still open. Commands and outputs
are recorded in the commit messages and the baseline section below.

## Checkpoint status

| Checkpoint | Status | Evidence |
|---|---|---|
| 1. Inventory | Done (informal) | Source survey of src/, include/, tests/, examples/, CI |
| 2. Baseline build and tests | Done for commit 2bdbd22 | See Baseline below |
| 3. Critical protocol and security | Done for listed items | Findings F1-F4 |
| 4. Transport, encryption, RPC | Partly done | Transport framing tested (11 cases); RPC deadline fixed; time-sync errors open (F6) |
| 5. Auth and session persistence | Partly done | Atomic save, 0600 perms; schema versioning and corruption detection open |
| 6. Networking, retries, backoff | Not started | Flood-wait parsing exists; backoff/jitter not implemented |
| 7. Update synchronization | Not started | |
| 8. Message, chat, media APIs | Partly done | 27 new API methods added in batches; live calls not tested |
| 9. Storage and schema generation | Partly done | Prepared statements reviewed; generator reproducibility not checked |
| 10. Tests, fuzzing, sanitizers | Partly done | 81 mtproto tests; 3 fuzz targets; ASan/UBSan CI job written, never run in CI |
| 11. Benchmarks | Not started | No numbers exist yet |
| 12. Docs, CI, release | Partly done | SECURITY.md, AUDIT.md, CHANGELOG.md, CI workflow (never executed) |

## Baseline (checkpoint 2)

Commit 2bdbd22, clean `git archive` copy, `cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON`,
`cmake --build -j1`, Ubuntu 24.04 sandbox, g++ with -O0 from the default build flags.

- Build: exit 0, 0 errors, 9 warnings (listed below).
- `ctest`: 100% passed, 147 of 147 tests.
- Debug build: not run yet.

Warnings at 2bdbd22:
- `client_more.cpp`: unused `next_random_id_more` (fixed in working tree).
- `auth.cpp`: unused `exchange` (fixed in working tree).
- `api_impl_3.cpp`, `api_impl_4.cpp`: unused `fl_flags` (generated code, open, see F7).
- `tests/mtproto/handshake_test.cpp`: deprecated OpenSSL RSA API (open, see F8).

## Findings

| ID | Severity | Status | Area | Finding | Evidence |
|---|---|---|---|---|---|
| F1 | High | Fixed | Handshake | Server DH params were not validated (prime size, safe prime, generator residue, g_a range) | `validate_dh_params` in `src/mtproto/auth.cpp`; `Handshake.*` tests |
| F2 | High | Fixed | Session storage | Session file and sqlite store were created with default umask (world-readable auth keys) | `src/secure_file.h`; `SecurityHardening.PrivateFileIsOwnerOnly` |
| F3 | Medium | Fixed | RPC | `invoke()` had no overall deadline; a server that kept sending unrelated packets blocked it forever | `MtSession::invoke` deadline, `Session.InvokeHonoursRequestTimeoutWhenServerKeepsTalking` (written after the fix, not run against the old code) |
| F4 | Medium | Fixed | Session storage | Session save truncated the live file in place; a crash mid-write could destroy the session | `save_session_file` temp + fsync + rename; `SessionStorage.AtomicSaveLeavesNoTempFileAndRoundTrips` |
| F5 | Low | Fixed | Randomness | `random_id` used `std::mt19937_64` seeded from `random_device` | `src/secure_random.h`, 7 client files |
| F6 | Medium | Open | MTProto | `bad_msg_notification` codes 16 and 17 (time not synchronized) are thrown as errors instead of adjusting the time offset and resending | `session.cpp` dispatch, `bad_msg_notification` case |
| F7 | Low | Open | Codegen | Generated `api_impl_3.cpp`/`api_impl_4.cpp` declare unused `fl_flags`; fix belongs in the generator | Baseline warnings |
| F8 | Low | Open | Tests | Handshake test uses deprecated RSA API (OpenSSL 3) | Baseline warnings |
| F9 | Low | Open | Transport | After a transport error (e.g. negative length word), the decoder stays in the error state; the caller must reconnect. Not documented in the header yet | `transport.cpp` `next()` |
| F10 | Low | Fixed | MTProto | msg_key verification used `operator!=` (not constant time) | `constant_time_equal`, used in `decrypt_packet` |

## Reviewed, no defect found

- TL reader: every read calls `require()`; `read_bytes` bounds-checks the padded length; vector counts are capped by remaining bytes (`tl.h`).
- Message-key and AES-IGE key derivation match the MTProto 2.0 formulas for client (x=0) and server (x=8).
- `parser.cpp` attribute loop always advances `pos`.
- Storage SQL uses prepared statements with bound parameters.
- Transport framing: abridged, intermediate and full handle partial input, truncation, CRC and length limits (tested).

## Fuzzing and sanitizers

- libFuzzer smoke runs (90 s each): TL reader 1.56M execs, session string 36M, HTML/Markdown parser 1.39M; no ASan/UBSan reports.
- Longer 10-minute runs were started and stopped early (about 6.1M, 67M and 3M execs), still no reports. A full-length run is still needed.
- ASan+UBSan build and tests: not run to completion in this sandbox.

## Not implemented (explicit)

- Exponential backoff with jitter for reconnects (checkpoint 6).
- Update difference and gap recovery (checkpoint 7).
- Encrypted session storage, session export/import.
- Benchmarks.
- Debug build baseline.
- Live Telegram testing of this batch (must run on the VPS with a dedicated test account).
