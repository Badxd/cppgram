# Test results: login state across DC migration (limitation 62)

Every result below comes from a command run in this pass, in the scratch build directory `build-audit` (GCC 13.3, Ninja,
Release, `-DENABLE_TESTS=ON`). Nothing was run against Telegram. The tests use loopback fake DCs, fictional numbers and
dummy credentials. Status key: **PASS**, **FAIL**, **SKIP**, **NOT RUN**.

## Totals

| Binary | Tests run | PASS | FAIL | SKIP | Exit code |
|---|---|---|---|---|---|
| `cppgram_mtproto_test` | 394 | 394 | 0 | 0 | 0 |
| `cppgram_async_test` | 46 | 46 | 0 | 0 | 0 |
| `cppgram_bot_test` | 110 | 110 | 0 | 0 | 0 |
| `cppgram_media_test` | 22 | 22 | 0 | 0 | 0 |
| `cppgram_metrics_test` | 23 | 23 | 0 | 0 | 0 |
| `cppgram_security_test` | 53 | 53 | 0 | 0 | 0 |
| **Total** | **648** | **648** | **0** | **0** | |

Previous release: 638 of 638. New in this pass: 10 tests (9 in `auth_migration_state_test.cpp`, 1 in `auth_state_test.cpp`).
No test was disabled or skipped. The `SKIP` count is zero because no test calls `GTEST_SKIP`.

## Build and checks

| # | Command | Result |
|---|---|---|
| 1 | `cmake --build <scratch>/build-audit --target cppgram_mtproto_test -j4` (full library rebuild, `client.h` changed) | **PASS**, exit 0, 73 of 73 steps |
| 2 | `cmake --build <scratch>/build-audit -j2` (every target: library, all tests, examples, benchmarks, fuzz) | **PASS**, exit 0. The build tree was already current (`ninja: no work to do`), so this confirms the state of the tree rather than building again |
| 3 | `python3 tools/check_cmake_sources.py` | **PASS**. Library 112 of 112 on disk, tests 67 of 67 on disk, 0 problems |
| 4 | `clang++-18 -std=c++20 -fsyntax-only -Wall -Wextra` on `src/login.cpp`, `src/client.cpp`, `src/auth_state.cpp` | **PASS** (syntax only, no link, no run). `login.cpp` and `auth_state.cpp` have 0 warnings. `client.cpp` has 1 warning, `-Wmissing-field-initializers` at line 1216 (`InputReplyTo`). That line is unchanged from the previous pass. The check ran without the project's generated include and define flags |

## Commands for the test runs

```
cppgram_mtproto_test                       # 394 PASS, exit 0
cppgram_async_test                         # 46 PASS, exit 0
cppgram_bot_test                           # 110 PASS, exit 0
cppgram_media_test                         # 22 PASS, exit 0
cppgram_metrics_test                       # 23 PASS, exit 0
cppgram_security_test                      # 53 PASS, exit 0
cppgram_mtproto_test --gtest_filter='AuthMigrationState*:PhoneMigration*:DcMigration*:AuthState*'   # 34 PASS
```

## New regression tests (`tests/mtproto/auth_migration_state_test.cpp`)

| Test | Requirement it covers | Result |
|---|---|---|
| `PhoneCodeSurvivesUnrelatedMigrationAndSignsInOnTheIssuingDc` | Code issued on DC 5. An unrelated `login_bot` is moved to DC 3 and fails there. Sign-in succeeds on DC 5. DC 3 sees no sign-in | **PASS** |
| `BotLoginMigrationStillWorksWithoutAPendingCode` | Bot login migration from DC 2 to DC 5 is unchanged | **PASS** |
| `BotLoginSuccessEndsAPendingPhoneLogin` | A successful bot login clears a pending code. Later sign-in is refused | **PASS** |
| `PhoneMigrationDuringSignInDropsTheCodeAndDoesNotSendItElsewhere` | `signIn` gets `PHONE_MIGRATE_5` for a code issued by DC 2. DC 5 gets no sign-in. A new code request then works | **PASS** |
| `PasswordRequiredSurvivesUnrelatedMigration` | 2FA challenge kept across a move. Password step goes back to DC 5. DC 3 gets none | **PASS** |
| `ResendAfterUnrelatedMigrationUsesTheIssuingDc` | Resend runs on DC 5. The new hash is accepted, the old hash is refused | **PASS** |
| `FailedMigrationLeavesAConsistentStateAndRecovers` | A failed hop leaves the client `Disconnected` with no pending code. The saved key is unchanged. With no saved session, `connect()` dials the restored DC 5. A new code then works | **PASS** |
| `RepeatedMigrationStopsAndKeepsThePendingCode` | Bot login bounces between DC 5 and DC 3. It stops after two hops (3 requests). The pending code is kept and sign-in works | **PASS** |
| `ExplicitDisconnectThenFreshLoginDropsTheOldCode` | `disconnect()` ends the login. After `connect()` the old hash is refused. A new code works | **PASS** |

Also in `tests/mtproto/auth_state_test.cpp`: `AuthState.LoginCanStartFromRecoverableError` **PASS**. The state table
now allows `LoginStarted` from `RecoverableError`, which `auth_can_login()` already accepted.

Updated in `tests/mtproto/phone_migration_fake_dc_test.cpp`: `SignInAfterAnotherMigrationStillUsesTheIssuingDc` **PASS**.
In the previous pass this test pinned the limitation. It now asserts the fixed behaviour.

Previous-pass tests, all still **PASS**: 8 phone-migration tests and 5 bot-login and saved-session tests (`DcMigrationFakeDc.*`,
`DcMigrationParse.*`, `PhoneMigrationFakeDc.*`).

## Mutation checks

Each mutation changed one file in the scratch copy. The binary was rebuilt, the filter `AuthMigrationState*:PhoneMigration*:DcMigration*:AuthState*` was run, and the file was restored. Every restored file was then compared byte for byte with a saved copy of the fixed file.

| Mutation | Change | Tests that failed | Result |
|---|---|---|---|
| **A** (required) | `sign_in_code` no longer goes back to the issuing DC before submitting the code | `AuthMigrationState.PhoneCodeSurvivesUnrelatedMigrationAndSignsInOnTheIssuingDc`, `PhoneMigrationFakeDc.SignInAfterAnotherMigrationStillUsesTheIssuingDc` | **KILLED** (2 FAIL) |
| B | `migrate_to_dc` no longer keeps the login state | 14 tests, including all phone-migration tests | **KILLED** (14 FAIL) |
| C | A failed hop no longer restores the previous DC settings | First run: `FailedMigrationLeavesAConsistentStateAndRecovers` did not fail, because the saved-session routing dialled DC 5 anyway. The test was then strengthened (session file removed before reconnect). Second run: that test FAILS | **KILLED after test fix** (1 FAIL) |
| D | `sign_in_password` no longer goes back to the issuing DC | `AuthMigrationState.PasswordRequiredSurvivesUnrelatedMigration` | **KILLED** (1 FAIL) |
| E | `resend_login_code` no longer goes back to the issuing DC | `AuthMigrationState.ResendAfterUnrelatedMigrationUsesTheIssuingDc` | **KILLED** (1 FAIL) |
| F | A successful bot login no longer clears a pending phone login | `AuthMigrationState.BotLoginSuccessEndsAPendingPhoneLogin` | **KILLED** (1 FAIL) |
| G | `sign_in_code` sends the code to the migrate DC (policy removed) | `AuthMigrationState.PhoneMigrationDuringSignInDropsTheCodeAndDoesNotSendItElsewhere` | **KILLED** (1 FAIL) |

Final sources were restored and checked: `login.cpp`, `client.cpp`, `auth_state.cpp` and `client.h` are byte-identical to the
fixed copies. The mtproto binary was rebuilt from them, and the full run in the table above was done on that build.

Mutation C initially survived. The test was then made to cover the case it was missing, and that is recorded here rather
than hidden.

## Not run

| Check | Status | Reason |
|---|---|---|
| AddressSanitizer | **NOT RUN** | The machine has 2 cores and 7 GB RAM. The repository notes a sanitizer build needs about 8 GB and about 1.5 hours at `-j1`. A full sanitizer build of the library was not attempted |
| UndefinedBehaviorSanitizer | **NOT RUN** | Same reason |
| ThreadSanitizer | **NOT RUN** | Same reason. The connect and disconnect paths touched here are not run concurrently by these tests |
| Clang full build and test run | **NOT RUN** | Only the Clang 18 syntax check above was run. No Clang build, link or test run |
| Valgrind | **NOT RUN** | Not run in this pass |
| `make checks`, `make format-check`, `make schema-check` | **NOT RUN** | Not rerun in this pass |
| `tools/check_examples.sh` | **NOT RUN** | The examples were built as part of the full build, but the example check script was not run |
| Live Telegram, phone login against real DCs | **NOT RUN** | No phone number, account or credential used. Restricted by the task |
| Connection drop on the same DC during a pending login | **NOT TESTED** | The fake DC cannot drop an open connection. See KNOWN_LIMITATIONS item 62 |

## Skipped

None. No test was skipped or disabled. No failure was hidden. The one initial survivor (mutation C) and the one wrong
expectation found during the pass (a disconnected client raising `runtime_error`, not `AuthenticationError`) are described above.

## Proxy and transport phase

Run on this tree with the existing `build-audit` directory (Release, `/usr/bin/c++` = GCC 13.3.0).

| Check | Command or source | Result |
|---|---|---|
| New proxy audit suite | `cppgram_mtproto_test` filtered to the proxy suites | 80 tests from 18 suites, PASS (includes the existing obfuscated, proxy secret, SOCKS5 and connection-net suites) |
| New file alone | `tests/mtproto/proxy_transport_audit_test.cpp` | 23 tests, PASS |
| mtproto binary, full | `cppgram_mtproto_test` | 417 PASSED, 0 failed. Includes the auth migration suites (bot, phone, DC, saved-session routing, pending code and 2FA across migration) |
| bot binary | `cppgram_bot_test` | 110 PASSED |
| media binary | `cppgram_media_test` | 22 PASSED |
| metrics binary | `cppgram_metrics_test` | 23 PASSED |
| security binary | `cppgram_security_test` | 53 PASSED |
| async binary | `cppgram_async_test` | 46 PASSED |
| Build, target | `cmake --build build-audit --target cppgram_mtproto_test -j2` | PASS, 0 warnings in the GCC build log |
| Build, all targets | `cmake --build build-audit -j2` | PASS (rc 0). **Incremental**, not a clean build |
| Clean build | from an empty directory | NOT RUN in this phase |
| Clang C++20 | `clang++ -std=c++20 -fsyntax-only -Wall -Wextra` on the test file, `obfuscated.cpp`, `connection.cpp`, `client.cpp` | Test file, `obfuscated.cpp`, `connection.cpp`: 0 errors, 0 warnings. `client.cpp`: 1 warning at line 1231 (`missing field 'top_msg_id'`), also present in the baseline. Syntax check only, not a Clang build |
| Clang full build | | NOT RUN |
| ASan / UBSan / TSan | | NOT RUN (2 cores and 7 GB, as the sanitizer scripts expect more; not attempted in this phase) |
| check_cmake_sources.py | | PASS (68 test names, all on disk) |
| check_headers.sh | | PASS (rc 0) |
| check_schema.sh (with codegen) | | PASS (rc 0) |
| check_examples.sh, check_version.sh | | PASS (rc 0) |
| format_check.sh | | PASS: 54 files, 0 failing. The new test file was added to the allowlist after formatting it |
| clang-format, changed src files | | NOT CLEAN. These files were already unformatted in the baseline (client.cpp 604, connection.cpp 39, obfuscated.cpp 31, connection.h 10, obfuscated.h 11, client.h 288 violations). This phase added 34 more violations, all in lines it wrote. They were not reformatted, to keep the diff focused |
| Live Telegram or proxy | | NOT RUN. No live operation was performed |

### Known-answer and stub tests

- `ProxyKnownAnswer`: 1 test, five vector sets (intermediate DC 2, padded DC 2, abridged DC 2, intermediate DC -2,
  intermediate DC 10002). Each checks the handshake, the framing, the encrypted frame and the decrypted reply.
- `ObfuscatedAbridged`, `ObfuscatedPadded`, `ObfuscatedFramingGuard`: framing length rules, padding range and
  randomness, and mismatched framing rejected before any socket.
- `ProxySecretParse`: prefixes, case, fake-TLS domain, malformed input with no echo of the input.
- `ProxyStub` (loopback MTProxy stub): intermediate, padded and abridged round trips, DC ids, fragmented reply, 1 MiB
  packet, -404 reply, silent proxy, early close, wrong secret, reconnect after failure.
- `Socks5Stub` (loopback SOCKS5 stub): fragmented method and reply, wrong version (greeting and reply), unknown ATYP,
  REP 5.

### Mutation testing

Each mutant was applied to one source file, the relevant suites were run, and the source was restored and checked with
`cmp`. Every restore was byte-exact.

| Mutant | Result | Restored byte-exact | Failing tests (first run) |
|---|---|---|---|
| M1 rx key from init instead of reversed init | KILLED | True | ProxyKnownAnswer.MatchesIndependentReferenceForEveryCase, ProxyStub.AbridgedRoundTripWithExplicitFraming, ProxyStub.DcIdIsCarriedInTheHandshake, ProxyStub.FragmentedReplyIsReassembled |
| M2 abridged short-form threshold 0x7e | KILLED | True | ObfuscatedAbridged.FrameLengthEncoding |
| M3 DC id bytes swapped | KILLED | True | ObfuscatedHandshake.ServerDecodesFramingAndDcForBothModes, ObfuscatedNet.PlainSecretRoundTripThroughScriptedProxy, ProxyKnownAnswer.MatchesIndependentReferenceForEveryCase, ProxyStub.DcIdIsCarriedInTheHandshake |
| M4 padded frame without padding | KILLED | True | ObfuscatedFrame.PaddedServerFramesDecryptToTheOriginalPacket, ObfuscatedPadded.PaddingIsZeroToFifteenBytesAndRandom |
| M5 silent-proxy diagnostic removed | KILLED | True | ProxyStub.SilentProxyIsAProxyErrorWithoutTheSecret |
| M6 send_all ignores partial sends | SURVIVED | True | none |
| M7 SOCKS5 reply version not checked | KILLED | True | Socks5Stub.WrongVersionInConnectReplyIsRejected |
| M8 dd prefix not recognised | KILLED | True | ProxySecret.PaddedIntermediateSecret |

- **M2** survived the first run. The existing test only covered 127 words, which is the first escape length. A
  126-word case was added (one byte, `0x7e`), and the mutant is now killed.
- **M5** could not be applied by the first runner because its pattern matched twice. It was rerun with the pattern
  anchored to `throw_receive_timeout`, and it is killed.
- **M6** survives and is treated as an equivalent mutant. On a blocking TCP socket, `send(2)` returns a partial count
  only when a signal interrupts it. No test in this suite can force that, so the mutant is not killed and is not claimed
  as killed.

Mutation coverage is 7 of 8 killed, with 1 equivalent survivor. This covers only the eight mutants above, not the whole
transport.

### Test harness fixes made during the phase

- The first draft of the abridged stub reply was 38 bytes. That is not a multiple of 4, so the abridged length was wrong.
  Fixed to 32 bytes. This was a fixture bug, not a cppgram bug.
- A stub thread threw from `write_all` when the client closed during a reply. That called `std::terminate` and killed the
  whole test binary. The stub threads now catch and end the connection. A stub failure still shows up as a missing reply
  in the test.
- `Connection::set_receive_timeout` throws before a socket is connected. The tests call it after `connect_obfuscated`.
