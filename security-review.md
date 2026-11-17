# Security review: CppGram 1.0.0 (release candidate)

Scope: the library code that handles server-controlled input (plain and encrypted packet parsing, transport framing,
MTProxy obfuscation, TL reading), flood-wait and DC-migration logic, session file handling, and logging. The live-test
project is reviewed separately in `CppGramLiveTest/SECURITY.md`.

This is an engineering review of this tree, with tests as evidence. It is not a third-party audit and does not replace
one.

Status values: PASS, FAIL, BLOCKED, NOT RUN, PENDING USER APPROVAL.

## Findings

| ID | Area | Finding | Status | Evidence |
|---|---|---|---|---|
| SR-1 | Plain packets | The attached snapshot rejected plain replies with more than 15 trailing bytes. Dd-proxy replies carry more padding (96 to 123 bytes were observed). This was a functional regression, which blocked `get_me` over dd proxies. | FIXED, PASS | The length field is authoritative. Only `length > remaining` is rejected. Tests `Crypto.TrailingTransportPaddingIsIgnoredAndTamperingIsRejected`, `Handshake.PlainPacketIgnoresTrailingPaddingOfAnySize`, `Handshake.PlainPacketRegressionLiveTrailingCounts`. Decision: `PROXY_AUDIT.md` section 7. |
| SR-2 | Plain packets | Trailing bytes after the length field are not authenticated. | ACCEPTED RISK, PASS | A plain packet has no MAC and never had one. The declared length still bounds what is read. Trailing bytes are dropped and never reach the TL layer. |
| SR-3 | Encrypted packets | The attached snapshot failed for 16 or more trailing bytes. The fix locates the message from its inner length and checks msg_key per candidate. | FIXED, PASS | `decrypt_packet` (`src/mtproto/crypto.cpp`). Tests `Crypto.ForgedInnerLengthIsRejected`, `ObfuscatedFrame.PaddingBeyondOneBlockIsIgnored`. Fuzz `fuzz_decrypt_packet`, 12.7 M executions, no reports. |
| SR-4 | Encrypted packets | A forged inner length could steer the candidate scan. | PASS | The inner length must fit in the decrypted buffer. Only candidates from the sender's padding grammar are tried (at most three extra 16-byte blocks). Each candidate is checked with a constant-time msg_key comparison. The false-accept probability per candidate is that of the 128-bit msg_key. |
| SR-5 | Encrypted packets | Accepting more padding widens the set of accepted encodings. | ACCEPTED RISK, PASS | Trailing bytes after the ciphertext are not part of the message. A changed byte in the message body fails msg_key. The padding grammar is documented in `PROXY_AUDIT.md`. |
| SR-6 | Transport framing | Oversized frames could drive large allocations. | PASS | Intermediate and abridged reject frames above 16 MiB. Tests `TransportFraming.AbridgedRejectsOversizedFrame` (suite name as in the tree), `IntermediateRejectsFrameOneByteOverMaxFrame`, `IntermediateAcceptsFrameExactlyAtMaxFrameBoundary`. Fuzz `fuzz_transport_framing`, 27.5 M executions, no reports. |
| SR-7 | Length fields | Unbounded allocation from a server-controlled length. A pattern reported for another MTProto library. | PASS for this library | Plain and encrypted inner lengths are checked against the bytes received. Other libraries were not tested. |
| SR-8 | Flood wait | A per-request cancel did not interrupt a FLOOD_WAIT sleep. | FIXED, PASS | `sleep_flood_wait` polls the request cancel token about every 50 ms. Tests `Session.FloodWaitSleepHonoursPerRequestCancel`, `Session.FloodWaitSleepCanBeCancelledWithoutResending`, `ClientFakeDc.StopCancelsAFloodWaitInProgress`. |
| SR-9 | Flood wait | A wait that would end after the request deadline was slept, then resent after the deadline. | FIXED, PASS | The deadline check runs before the sleep and returns the error to the caller. Test `Session.FloodWaitRetryLoopIsBoundedByAttemptBudget`, `Session.FloodWaitSleepIsNotTakenPastRequestDeadline`. |
| SR-10 | Flood wait | Retry loops could run without bound. | PASS | The attempt budget and the policy's maximum wait bound it. Test `Retry.FloodWaitDigitBoundaryAndStrictFormat`. |
| SR-11 | DC migration | `migrate_dc_from_error` used `std::atoi` on server text. Overflow is undefined. | FIXED, PASS | `std::from_chars` accepts 1..99 only. Overflow and sign tests in `dc_migration_fake_dc_test.cpp`. |
| SR-12 | Session file | A corrupted session file could be silently replaced. | PASS | The CGS2 format ends with a CRC-32 and a damaged file throws `SessionCorruptError`. Test `offline.session_roundtrip` (live project) and session tests. |
| SR-13 | Secrets in logs | API hashes, bot tokens and session data must not appear in logs or reports. | PASS (code review and tests) | Redaction tests `PrometheusFormat.TokenLikeLabelValuesAreRedacted`. The live runner prints only names, stages and counts. |
| SR-14 | Repository contents | Secrets or private keys in the tree. | PASS for the scan | The scan found only dummy fixtures (`123456789:AAExample...`, `0123...` hashes) in test files. No private keys. |
| SR-15 | Fake DC | The fake DC used by tests. | PASS | It binds to loopback. |
| SR-16 | Fake-TLS secrets | A fake-TLS secret could be used without the library noticing. | PASS | Refused before any network use with `std::invalid_argument`. |
| SR-17 | Credential rotation | Credentials used during development. | PENDING USER APPROVAL | Owner decision. Nothing rotated. |
| SR-18 | Public exposure | The telemt proxy on public 8443 (classic and secure modes). | PENDING USER APPROVAL | Owner decision. Not changed. |
| SR-19 | Public repo binary | `bin/cppgram_live_test_bot` is committed in the public Cpplivetest repository. | PENDING USER APPROVAL | Owner decision. The live archive does not include it. |
| SR-20 | Live behaviour | No live Telegram traffic has been validated in this release. | BLOCKED | See `KNOWN_LIMITATIONS.md` RB-1. |

## Limits of this review

- Live Telegram behaviour: BLOCKED. No live result is claimed.
- Fuzzing: seven targets, 120 s each, library instrumented, no findings (`TEST_RESULTS.md`, section 4). Bounded runs only.
- TSan covers all six test binaries (711 tests). ASan and UBSan cover the full suite (711 of 711).
- The malformed-padding grammar sub-case is only partly covered.
- TDLib parity of the trailing-byte handling is BLOCKED.
