# CppGram: login state across DC migration (private report)

**Status in one line:** a pending phone code and a pending 2FA challenge now survive a DC migration caused by another
request, and the code is submitted to the DC that issued it. 648 of 648 offline tests pass across the six binaries (up
from 638). Failed migrations leave a consistent state. **Live Telegram: NOT RUN.** No real phone number, account or
credential was used. Sanitizers and Clang builds were not run (see below).

## Trace: which transitions keep the login, which reset it

| Path | Where | Login state | Reason |
|---|---|---|---|
| `connect()` (user) | `src/client.cpp` | **Reset** (`login_dc_ = 0`, state `Unauthenticated`) | An explicit connect starts a fresh session |
| `disconnect()` (user, destructor, `log_out()`, `connect_async` path) | `src/client.cpp` | **Reset** (`login_dc_ = 0`, state `Disconnected`) | An explicit disconnect ends the login |
| `establish_connection()` (new, internal) | `src/client.cpp` | Kept (no login change) | Used by migration. Drops the old connection, then `connect_impl()` |
| `drop_connection()` (new, internal) | `src/client.cpp` | Kept | Closes the session and sets `Disconnected`. Used by `disconnect()` and failed connects |
| `migrate_to_dc()` | `src/client.cpp` | **Kept** for `CodeRequired`, `PasswordRequired`, `Authenticating`. Other states reset | An unrelated migration must not lose the code. Failure path below |
| `migrate_to_dc()` failure | `src/client.cpp` | **Cleared** (`login_dc_ = 0`), previous DC settings restored, `force_new_auth_key_` reset, saved key untouched, state `Disconnected` | Consistent state. The pending code is not known to be usable |
| `reconnect_after_drop()` (same DC) | `src/client.cpp` | Reset, as before (calls `connect()`) | Not changed. Not testable with the fake DC (limitation 62) |
| `login_bot()` migration hops | `src/client.cpp` | Kept during the hops (through `migrate_to_dc()`) | Bot-login migration unchanged |
| `login_bot()` success | `src/client.cpp` | **Ends a pending phone login** (`login_dc_ = 0`, `CodeRequired`/`PasswordRequired` → `Unauthenticated`) | The session now belongs to the bot |
| `send_login_code()` | `src/login.cpp` | `login_dc_ = 0` at start, set to the answering DC on success | A new code supersedes an older one |
| `sign_in_code()` | `src/login.cpp` | Moves to `login_dc_` **before** `LoginStarted`, then submits the code | The hash is valid only on its issuing DC |
| `sign_in_code()` migrate reply | `src/login.cpp` | **Pending code dropped** (`login_dc_ = 0`), error `AuthenticationError`. The code is not sent to the other DC | Decision, see below |
| `sign_in_password()` | `src/login.cpp` | Moves to `login_dc_` first; `login_dc_` cleared on success | The 2FA challenge belongs to the DC that accepted the sign-in |
| `resend_login_code()` | `src/login.cpp` | With a pending login, moves to `login_dc_` first and records the answering DC for the new hash. Without one, unchanged | The resend belongs to the pending code |
| `call_login_rpc()` | `include/cppgram/client.h` | Simplified. Relies on `migrate_to_dc()` to keep the state | One place owns the rule |

## Decisions made in this pass (review these)

1. **A sign-in migrate reply drops the code.** If `auth.signIn` answers `PHONE_MIGRATE_N` for a code issued by another DC,
   the code is not sent to DC N. The reasoning: a hash is valid only on the DC that issued it, and a migrate reply means the
   issuing DC is not the account's home. I could not confirm this against a live server. The alternative was to resend the
   same hash to DC N. The current choice is stricter. KNOWN_LIMITATIONS item 78.
2. **A successful bot login ends a pending phone login.** Before this pass a bot login left the phone state in place.
3. **A failed migration clears the pending code.** The state is consistent, but the user must send a new code. KNOWN_LIMITATIONS item 77.
4. **A login can start from `RecoverableError`.** The transition table rejected `LoginStarted` from that state, even though
   `auth_can_login()` accepts it. This was a latent bug: the next login step failed with `logic_error`.

## Results

| Item | Result | Where |
|---|---|---|
| Offline tests, six binaries | 648 run, 648 PASS, 0 FAIL, 0 SKIP | `TEST_RESULTS.md` |
| New migration-state tests | 9 added (`tests/mtproto/auth_migration_state_test.cpp`), all PASS | `TEST_RESULTS.md` |
| State-table test | 1 added (`AuthState.LoginCanStartFromRecoverableError`), PASS | `TEST_RESULTS.md` |
| Previous phone and bot-login tests | All still PASS (one pinned test updated to the fixed behaviour) | `TEST_RESULTS.md` |
| Mutation checks | 7 of 7 killed. Mutation A (removing the issuing-DC restoration in `sign_in_code`) fails 2 tests. One mutant survived at first and was killed after the test was strengthened | `TEST_RESULTS.md` |
| Full build, every target | Exit 0 (the tree was already current) | `TEST_RESULTS.md` |
| Source list check | 112 library, 67 test sources, 0 problems | `TEST_RESULTS.md` |
| Clang 18 syntax check, changed sources | Pass (syntax and warnings only). One pre-existing warning in an unchanged line | `TEST_RESULTS.md` |
| ASan, UBSan, TSan, Clang full build, Valgrind | **NOT RUN.** 2 cores and 7 GB RAM. Full sanitizer builds need about 8 GB and about 1.5 hours at `-j1` | `TEST_RESULTS.md` |
| Live Telegram | **NOT RUN** | `TEST_RESULTS.md` |

## What is not claimed

- The fake DCs answer with the replies this pass expects from the TL schema. They show that CppGram reacts to those replies.
  They do not show that Telegram sends them. Decision 1 in particular is unverified against a live server.
- The 2FA step is checked up to a wrong-password reply. A correct password and the full SRP login after migration are not
  tested.
- A connection drop on the same DC still clears a pending code (KNOWN_LIMITATIONS item 62). This was not changed.
- Other RPCs are still not retried on `USER_MIGRATE_N` (item 63). Retrying an arbitrary request on a new DC needs its own
  design, because the key on the new DC has not been shown to be authorized for the account. That was not checked live.
- Cancellation is still not honoured during a migration's TCP connect (item 73).

## Owner next steps

1. Review decisions 1 to 3 above, especially the sign-in migrate policy. Decision 1 is the one most likely to need a change
   after a live test.
2. Decide whether to keep the login across a same-DC reconnect (item 62).
3. Run the sanitizer builds and `make checks` before any release. They were not run here.

## Proxy and transport phase

### Result

The MTProxy path and the SOCKS5 path were audited offline. The wire format matches two independent implementations, and no
client defect was found. The earlier live receive timeout is **not** explained, and **it is not fixed**. The changes in
this phase are:

- An abridged framing for the obfuscated transport, with an explicit framing option on `MtprotoProxyOptions`.
- Framing validated before any socket is opened.
- Diagnostics that replace the generic receive timeout with a `ProxyError` after a silent or early-closing proxy.
- A new test file with known-answer vectors, two loopback stubs, and 23 tests.

Live compatibility is **not verified**. Do not claim it until a run on your own VPS passes the steps in
`VPS_LIVE_TEST_GUIDE.md`.

### Status by requested area

| Area | Status |
|---|---|
| Proxy architecture audit | Done. See `PROXY_AUDIT.md` |
| MTProxy compatibility | Checked offline against a reference and a stub. Live: NOT RUN |
| Secret handling | Hex only. Invalid input refused without echo (tested). Not wiped from memory (P5) |
| SOCKS5 | Greeting, no-auth, CONNECT replies and error codes tested with a stub. User and password path not tested (P9) |
| Offline loopback tests | 23 new tests, all PASS. Existing suites still PASS |
| Mutation testing | 8 mutants. 7 killed, 1 equivalent survivor (M6). See `TEST_RESULTS.md` |
| Build and verification | Incremental build PASS. Clean build, full Clang build, and sanitizers NOT RUN |
| Live VPS | NOT RUN. Guide written: `VPS_LIVE_TEST_GUIDE.md` |

### Auth migration check

The previous fixes were checked by running the existing suites: bot `USER_MIGRATE_N`, phone `PHONE_MIGRATE_N` with the
pending code and 2FA state, saved-session DC routing and auth state recovery. All are in `cppgram_mtproto_test`, which
passed 417 of 417.

### Not done

- No commit, push, pull request or GitHub Actions run.
- No live Telegram, proxy or credential use.
- The sanitizer and clean-build steps were not run.
