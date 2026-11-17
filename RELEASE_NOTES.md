# CppGram 1.0.0 release notes (release candidate)

**Status: release candidate. Not published. Not declared stable.** The decision, the blockers and the owner actions are in
`FINAL_V1.0.0_READINESS_REPORT.md`.

CppGram is a C++20 library for Telegram. It has two parts:

- `cppgram::Client`: an MTProto client that logs in as a bot or a user and talks to Telegram directly.
- `cppgram::bot`: an HTTP Bot API layer.

## What changed since 0.1.0

- **Plain replies over MTProxy (dd mode).** Replies with trailing transport padding were rejected. The attached snapshot
  allowed at most 15 trailing bytes, and dd-mode replies carry 96 to 123. The length field is now authoritative.
- **Encrypted packets over padded transport.** The message is located from its inner length and authenticated by
  msg_key, so trailing padding no longer fails decryption. Forged inner lengths are rejected.
- **FLOOD_WAIT.** A per-request cancel now interrupts the sleep within about 50 ms. A wait that would end after the request
  deadline is returned to the caller and not resent.
- **DC migration.** The DC number is parsed with `std::from_chars`, which is safe on overflow.
- **Build.** `VERSION` is a configure dependency, and the version is 1.0.0 in `VERSION` and `client.h`.
- **Tests and fuzzing.** New tests for the above. New libFuzzer harnesses for plain unwrap, encrypted decrypt and transport
  framing.

Details are in `CHANGELOG.md`.

## Verified in this release

- 711 of 711 CTest cases with GCC 13.3.0 and Clang 18.1.3, Release, C++20, no warnings.
- ASan and UBSan: 711 of 711, no reports.
- TSan: all six test binaries, 711 tests, no reports.
- Seven libFuzzer targets, 120 s each on an instrumented library: 36,962,006 executions, no findings.
- `cmake --install` and an independent consumer with `find_package(CppGram 1.0.0 EXACT)`, built and run with GCC and Clang.
- Static analysis: clang-tidy-18 reports 304 warnings, none confirmed as defects after triage. Doxygen 1.9.8 builds with 899 warnings. Neither is fixed in this release.
- `make schema-check` passes. The TL code was regenerated to match the generator.

Commands and counts are in `TEST_RESULTS.md`.

## Not verified

- **Live Telegram.** Every live result in this release is BLOCKED. Run the live-test project on an unrestricted network to
  verify login, sessions, reconnect and dd proxies (see `CppGramLiveTest/`).
- ARM64. Windows is unsupported.
- Long fuzz campaigns and MSan.
- TDLib parity of the plain trailing-byte handling is BLOCKED.

## Upgrade notes

- Code built against 0.1.0 should rebuild without source changes. The public API has no removals in this release.
- The client announces `app_version` 1.0.0.

## Known limits

See `KNOWN_LIMITATIONS.md`: fake-TLS secrets are refused, secret chats and calls are not implemented,
and the encrypted path accepts at most three extra padding blocks.
