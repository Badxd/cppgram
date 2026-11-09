# Implementation status

This is the status by area for the pre-VPS release, version 0.1.0. The numbers and limits are in the files named in each
row. Nothing here was run against Telegram.

Status terms:

- **Offline-tested**: built, and a named offline test passes against a fake DC or a local socket. The 624-case run is
  historical evidence from the final source pass. It was not rerun for this pre-VPS delivery.
- **Source only**: built, with no test naming it.
- **With limitations**: built, with the limits listed in `KNOWN_LIMITATIONS.md`.
- **Not in scope**: deliberately not implemented.
- **Live**: NOT RUN. Nothing in this release was run against Telegram.

## Status by area

| Area | Status | Evidence | Remaining limits |
|---|---|---|---|
| Client core, connect, disconnect, async operations | Offline-tested | `client_fake_dc_test.cpp`; `tests/async/async_test.cpp` (`cppgram_async_test`, 46 of 46) | Live NOT RUN |
| MTProto transport, framing, crypto | Offline-tested | `transport_test.cpp`, `connection_net_test.cpp`, `retry_test.cpp` | No hardening audit (`KNOWN_LIMITATIONS.md` 32) |
| MTProto proxy: plain and padded secrets, SOCKS5 | Offline-tested | framing, obfuscation and proxy tests, `VPS_TESTING_GUIDE.md` section M | Live NOT RUN |
| MTProto proxy: Fake-TLS (`ee`) secrets | Not in scope | rejected with an error | Unsupported by design (item 6) |
| Auth: login code, 2FA password, recovery | Offline-tested | `auth_state_test.cpp`, `password_recovery_fake_dc_test.cpp` | `recover_password` with a new password not verified live (item 21) |
| Auth: QR login, authorization import | Offline-tested | `qr_login_fake_dc_test.cpp` | Not verified live (item 22) |
| Session file handling | Offline-tested | `session_crypto_test.cpp`, `storage_test.cpp`, `storage_concurrency_test.cpp` | File permissions and encryption not re-audited (item 36) |
| Web App `initData` validation | Offline-tested | `tests/security/webapp_test.cpp` (`cppgram_security_test`, 53 of 53) | Not a cryptographic audit (item 35) |
| Messages: send, edit, history, search, forward, reactions, pins | Source only for most entries | `PARITY_MATRIX.md` (326 IMPLEMENTED, 173 source only) | Behaviour against Kurigram not audited |
| Ephemeral messages: send and edit | With limitations | `ephemeral_inline_fake_dc_test.cpp`, `ephemeral_upload_fake_dc_test.cpp` | Items 11 and 12 |
| Media upload and download | Offline-tested | `media_fake_dc_test.cpp`; `cppgram_media_test` 22 of 22 | Story big-file path untested (item 29); live NOT RUN |
| Inline results: send and decode | With limitations for rich results | `inline_send_fake_dc_test.cpp`, `inline_decode_fake_dc_test.cpp` | Items 13 to 17 |
| Rich messages: bodies and local files | Offline-tested | `rich_blocks_fake_dc_test.cpp` (six cases) | Items 12 and 13 |
| Keyboards: inline and reply, all variants | Offline-tested, unsupported variants throw | `keyboard_roundtrip_test.cpp` | Item 16 |
| Invoices in inline results | With limitations | `inline_send_fake_dc_test.cpp` | Nothing here contacts a payment provider (item 14) |
| Stories | Offline-tested for part of the surface; the full path is source only | `stories_fake_dc_test.cpp`, `stories_edit_fake_dc_test.cpp`; the not-modified path is in `auth_stories_batch4_test.cpp` | Items 24 and 25 |
| Stars, gifts, payment forms | Source only for most entries | `PARITY_MATRIX.md` | Behaviour not audited |
| Folders and chatlists | Offline-tested | `folders_audit_fake_dc_test.cpp` | Item 27 |
| Bots (Bot API surface) | Offline-tested for the bot test suite | `cppgram_bot_test` 110 of 110 | Item 10; generator not in repo (item 50) |
| DC migration | No dedicated test | none | Item 31 |
| Secret chats and calls | Not in scope | not among the 330 reference entries | Item 5 |
| `channels.UpdatePinnedForumTopic` | Schema-blocked | not in layer 233 | Item 4 |
| Schema code generation | Reproducible | `tools/check_schema.sh` with `schema/SHA256SUMS.txt`; 19 `api_impl` files | Provenance conflict (item 48) |
| Parity classification | Source and evidence only | `PARITY_MATRIX.md`, 330 rows | Not a behavioural audit |
| Public API documentation | Name-level index checked | `docs/API.md`, `docs/API_INDEX.md` (470 `Client` members, checked by `tools/api_index.py --check`) | Signatures and behaviour not tested by the index (item 58) |
| Portability | Documented, Linux x86_64 only | `docs/PORTABILITY.md` | ARM64, cross build and Windows not verified (items 53 to 55) |

## Build, verification and release preparation

- Build (historical, final source pass): GCC 13.3 Release, exit 0. Largest single compile about 2.8 GB. See
  `BUILD_RESULTS.md`.
- Tests (historical, final source pass): 624 of 624 offline test cases pass across six binaries. See `TEST_RESULTS.md`.
- Pre-VPS checks run in this pass: `make checks` exit 0 (source list, version, format allowlist, API index, example
  syntax); fresh configure exit 0; reconfigure and install exit 0 (55 headers, package files, `libcppgram.a`); consumer
  `find_package(cppgram)` project built, linked and run, exit 0; `cppgramConfigVersion.cmake` reports 0.1.0.
- Not run in this pass: the 624-case test suite, sanitizers (ASan, UBSan, TSan), Valgrind, ARM64, the 1 GB VPS and every
  live step.
- CI and release workflows (`.github/workflows/ci.yml`, `release.yml`, `live.yml`): written and YAML-checked. NOT RUN.
  The live workflow is manual and secret-gated, and was never executed.

## Owner next steps

The owner runs the live and VPS steps in `VPS_TESTING_GUIDE.md`, steps E to Q, and records the results there. This pass
did not run them and did not use credentials. The release package is built with `make package` and checked with
`make verify-release`.
