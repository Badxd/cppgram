# Security review: CppGram 0.1.0 pre-release (baseline 1cd09d0)

Scope: the library code paths that handle server-controlled input, the flood-wait and DC-migration logic, the MTProxy
plain and dd framing, the bot and webapp code in the public Cpplivetest project, the live-test tooling, and the
repository hygiene (secrets, binaries, logs). This is an engineering review of the code and tests in this tree. It is
not a third-party audit and it does not replace one.

Status values follow the release rules: PASS, FAIL, BLOCKED, NOT OBSERVED, NOT RUN, PENDING USER APPROVAL.

## Findings

| ID | Area | Finding | Status | Evidence |
|---|---|---|---|---|
| SR-1 | Flood wait | A per-request cancel did not interrupt a FLOOD_WAIT sleep. The sleep ran for the full server-given wait. | FIXED, PASS | `sleep_flood_wait` in `src/mtproto/session.cpp` polls the cancel token every 50 ms. Tests `FloodWaitSleepHonoursPerRequestCancel` and `FloodWaitSleepCanBeCancelledWithoutResending` pass. `ClientFakeDc.StopCancelsAFloodWaitInProgress` passes. |
| SR-2 | Flood wait | A FLOOD_WAIT whose wait ended after the request deadline was slept on and then resent. | FIXED, PASS | `fits_deadline` check before sleeping; the error is rethrown to the caller instead. Test `Session.FloodWaitRetryLoopIsBoundedByAttemptBudget` passes. |
| SR-3 | DC migration | `migrate_dc_from_error` parsed server text with `std::atoi`. Overflow is undefined behaviour. | FIXED, PASS | `src/dc.cpp` uses `std::from_chars` and accepts only 1..99. Overflow and sign tests are in `dc_migration_fake_dc_test.cpp`. |
| SR-4 | Length fields | Unbounded allocation from a server-controlled length, a pattern reported for another MTProto library (the advisory identifier was not re-verified in this round). | PASS (pattern not present in the CppGram plain path; checked by the tests cited) | The plain and encrypted inner length is checked against the bytes actually received. The intermediate and abridged transports reject frames over 16 MiB. Tests: `AbridgedRejectsOversizedFrame`, `IntermediateRejectsFrameOneByteOverMaxFrame`, `IntermediateAcceptsFrameExactlyAtMaxFrameBoundary`. Other libraries were not re-tested here. |
| SR-5 | Credentials | Secret rotation for the VPS and the live-test credentials. | PENDING USER APPROVAL | Nothing was rotated. No live service was touched. |
| SR-6 | Network exposure | The telemt proxy listens on public 8443 in classic and secure modes. | PENDING USER APPROVAL | Decision belongs to the owner. Not changed. |
| SR-7 | Public repo hygiene | The public Cpplivetest repository commits a 10.8 MB binary under `bin/`. | PENDING USER APPROVAL (owner action) | A local patch removes it: `pad-fix/mission4/cpplivetest-docs-and-build-fixes.patch`. The patch was not pushed. Binary removal is an owner action. |
| SR-8 | Public bot docs | Public bot docs described stale commands and paths. | PENDING USER APPROVAL (local patch applies cleanly to b8db65c; not pushed, not applied upstream) | Same local patch. |
| SR-9 | Webapp verification | Webapp init-data check. | PASS (code review only, no live test) | HMAC comparison is constant time, freshness is checked after the signature, and the token is not logged. Code review only; no live test. |
| SR-10 | Logging | Redaction of secrets in logs. | PASS (code review only, no live test) | The redaction key list was reviewed. Phone numbers are not logged. |
| SR-11 | Repo contents | Sensitive files in the tree. | PASS for the scan | No credential files in the tree. The secret scan in the final package is a separate step; see `FINAL_REPORT.md`. |
| SR-12 | Fake DC | The fake DC used for offline tests. | PASS | It binds to loopback only. |

## Limits of this review

- Live Telegram behaviour was NOT RUN. No credentials were used. Live results are not claimed.
- The fuzz campaigns are short runs (see `TEST_RESULTS.md`). They are not a proof of absence of defects.
- TDLib parity of the trailing-byte handling is BLOCKED: the upstream source was not reachable from the fetch allowlist.
- ARM64 is NOT RUN.
- The malformed-padding grammar sub-case is only partly covered (documented gap).

## Open items

1. SR-5 secret rotation: PENDING USER APPROVAL.
2. SR-6 telemt 8443 classic and secure modes: PENDING USER APPROVAL.
3. SR-7 binary removal from the public Cpplivetest repository: owner action.
