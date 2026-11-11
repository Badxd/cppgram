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
# CppGram: MTProxy secret format compatibility

## Result

`parse_proxy_secret()` now accepts Base64URL and standard Base64 secrets, in addition to the existing hex forms. The
decoded bytes are classified with the same rules as TDLib, Telegram's official library. Plain and `dd` secrets work in
every accepted text form. Fake-TLS (`ee`) secrets now parse, and the transport still refuses them, as the task requires.

**This does not by itself make your proxy work.** I did not receive your secret, so I could not tell which form it
uses. What it decodes to decides the outcome:

| What the live proxy check reports | Meaning |
|---|---|
| `SKIP: set TG_API_ID and TG_API_HASH` | The secret parsed as plain or `dd`. The next step is the connection itself (see `VPS_LIVE_TEST_GUIDE.md`). |
| `NOT_SUPPORTED: fake-TLS (ee) proxy secrets are not supported` | The secret is fake-TLS. cppgram cannot connect with it. Supporting it needs a fake-TLS transport, which is a separate, large piece of work and is not in this change. |
| `FAIL: TG_MTPROTO_PROXY_SECRET was not accepted: <reason>` | The text is not a form the parser knows. The reason names the rule that failed and never repeats the secret. |

Telegram Android showing "Connected" makes fake-TLS a likely case, because Telegram's own clients use it heavily. That is
an inference, not something I verified against your server.

## Verified format facts and sources

| Source | What it establishes | Status |
|---|---|---|
| Official MTProxy transport documentation, `core.telegram.org/mtproto/mtproto-transports` (fetched) | 16-byte secret, usually given in hex. A 17-byte variant whose first byte `0xdd` selects padded intermediate, and that byte is not part of the key. The page does not describe `ee` or Base64. | Verified |
| Third-party MTProxy listener manual, `manual.nssurge.com/others/mtproto.md` (fetched) | The listener takes a 32-hex-character secret, with an optional `dd` prefix. It says the `ee` fake-TLS format is not supported by that listener. Base64 appears only in a DC configuration file, not in the secret. | Third-party, not official. Used as corroboration only |
| TDLib source, `td/mtproto/ProxySecret.cpp` (local copy, read) | `from_link` tries hex, then Base64URL, then Base64. `from_binary` classifies the bytes: 16 is plain, 17 starting `0xdd` is padded, 18 or more starting `0xee` is fake-TLS. Fake-TLS secrets are written out as Base64URL. | Verified against the code |
| TDLib base64 decoder (`td/utils`) | Not in the local source copy. | **Not verified** |

No live server and no Telegram Android source were available. The base64 padding and trailing-bit rules are my choice
(strict), not something checked against TDLib's decoder.

## Accepted forms and decoding

Tried in this order, which is TDLib's order:

1. **Hex.** Any even-length string made only of hex digits. If it matches, nothing else is tried, so a wrong length is
   rejected rather than reread as Base64.
2. **Base64URL** (RFC 4648 section 5, `-` and `_`).
3. **Standard Base64** (RFC 4648 section 4, `+` and `/`).

Base64 rules: padding is optional. If present it must be at the end, at most two characters, and the total length must
be a multiple of 4. A length of 4k+1 is never valid. Trailing bits after the last full byte must be zero. Whitespace and
any other character are rejected. Nothing is trimmed, so a copied secret with a trailing space is refused, not fixed.

The decoded bytes are then classified:

| Decoded length and prefix | Kind | Key | Framing (if it reaches the transport) |
|---|---|---|---|
| 16 bytes, any prefix | Plain | bytes 0..15 | Intermediate |
| 17 bytes, first byte `0xdd` | Padded intermediate | bytes 1..16 | Padded intermediate |
| 18 or more bytes, first byte `0xee` | Fake-TLS | bytes 1..16, domain = the rest (1 to 253 printable ASCII bytes) | None: refused |
| Under 16 bytes, 17 bytes without a prefix, `0xdd` with the wrong length, unknown prefix | Rejected | | |

Hex fake-TLS text (`ee` + 32 hex + hex domain) decodes to the same bytes, so the hex and Base64 forms agree.

## Security

- Decoded bytes are held in a buffer that is overwritten when it goes out of scope. Each decoder reserves its full output
  size first, so the buffer never reallocates and leaves an unwiped copy behind.
- The caller's secret string is not modified or copied beyond what the parser needs.
- Errors name the rule that failed and the decoded length. They never include the input or any decoded byte. Tests check
  this for malformed input, and for the wrong secret at connect time.
- Input is capped at 540 characters before any decoding, the largest valid fake-TLS form.
- The live test prints the parser's message on failure. The message never contains the secret.
- Test vectors are synthetic. The key is made up, and the expected strings come from Python's `base64` module. No real
  secret is in the repository, the tests, or the logs. The proxy host you gave is not in the tree.
- Not done: the parsed `ProxySecret` keeps the 16-byte key in memory until it is destroyed. Only the temporary decode
  buffers are wiped. See `KNOWN_LIMITATIONS.md` (P5).

## Behaviour that could change

- A Base64 secret made only of hex digits, with even length, is read as hex. TDLib does the same. Such a secret is rejected
  if its length is wrong, not read as Base64. The chance of this for a real base64 secret is very small, but it is possible.
- Base64 with non-zero trailing bits is rejected. A non-canonical encoder would be refused, and TDLib's behaviour there is
  unverified.
- Any text that previously failed for one reason may now fail for another. The regression tests check outcome (accepted
  or rejected), not the message.

## Changes

| File | Change |
|---|---|
| `include/cppgram/mtproto/proxy_secret.h` | Adds `ProxySecretEncoding` and a `encoding` field on `ProxySecret`. Rewrites the format comment |
| `src/mtproto/proxy_secret.cpp` | Rewritten: hex, Base64URL and Base64 decoders, classification, and a wiping decode buffer |
| `tests/mtproto/proxy_secret_test.cpp` | Existing tests unchanged. Adds 13 tests: Base64 and Base64URL forms, alphabet strictness, malformed input, decoded-length rules, fake-TLS domain limits, oversized input, hex-first rule, no-echo, regression |
| `tests/mtproto/proxy_transport_audit_test.cpp` | Adds 3 end-to-end tests: padded Base64 selects padded framing on the wire, plain standard Base64 selects intermediate, fake-TLS Base64 is still refused |
| `examples/live_tests/live_tests.cpp` | Failure message for a rejected secret now includes the parser's reason. Env var names unchanged: `TG_MTPROTO_PROXY_HOST`, `TG_MTPROTO_PROXY_PORT`, `TG_MTPROTO_PROXY_SECRET`, `TG_DC` |
| `tools/format_allowlist.txt` | Adds the three rewritten files |
| `PROXY_AUDIT.md`, `KNOWN_LIMITATIONS.md`, `VPS_LIVE_TEST_GUIDE.md` | Updated for the new formats |

`src/client.cpp` and `src/mtproto/connection.cpp` were not changed. Both still refuse fake-TLS with the same message. The
transport, framing selection and handshake are unchanged.

`TG_DC` is read by the live test as before (default 2, must be 1 to 5). This change does not touch it.

## Tests

| Check | Result |
|---|---|
| Full offline CTest | 687 of 687 passed |
| `cppgram_mtproto_test` | 433 PASSED (417 before, 16 new) |
| `cppgram_bot_test`, `media`, `metrics`, `security`, `async` | 110, 22, 23, 53, 46 PASSED |
| Proxy secret suites | 28 PASSED |
| Mutation testing of the parser | 8 mutants, 7 killed, 1 survived by design (wipe, not observable from tests) |
| Offline live proxy check | four synthetic secrets, results as in the table above |
| GCC build, Clang syntax, format check | 0 warnings, clean, 57 files 0 failing |
| Live VPS, real credentials, Telegram | NOT RUN |
| Sanitizers, clean build | NOT RUN in this phase |

Mutation results:

| Mutant | Result | Restored byte-exact | Caught by (first run) |
|---|---|---|---|
| P1 url alphabet never tried | KILLED | True | ProxySecretBase64.FakeTlsSecretKeepsTheDomain, ProxySecretBase64.PaddedIntermediateSecretInBothAlphabets, ProxySecretBase64.PlainSecretInBase64URLPaddedAndUnpadded, ProxySecretBase64Transport.FakeTlsBase64SecretIsStillRefused, ProxySecretBase64Transport.PaddedBase64SecretSelectsPaddedFraming |
| P2 trailing-bit check removed | KILLED | True | ProxySecretBase64.MalformedBase64IsRejected |
| P3 length mod 4 == 1 not rejected | KILLED | True | ProxySecretBase64.DecodedLengthAndPrefixAreChecked |
| P4 domain printable check removed | KILLED | True | ProxySecret.FakeTlsDomainRules, ProxySecretRegression.ExistingHexRejectionsStillHold |
| P5 dd length check removed | KILLED | True | ProxySecret.RejectsWrongLengths, ProxySecretBase64.DecodedLengthAndPrefixAreChecked, ProxySecretRegression.ExistingHexRejectionsStillHold |
| P6 hex-first rule removed | KILLED | True | ProxySecret.FakeTlsDomainRules, ProxySecret.FakeTlsSecretCarriesDomain, ProxySecret.PaddedIntermediateSecret, ProxySecret.PlainSecretParsesBothCases, ProxySecret.RejectsWrongLengths |
| P7 base64 padding not checked | KILLED | True | ProxySecretBase64.MalformedBase64IsRejected |
| P8 wipe removed (not observable by tests) | SURVIVED | True | none |

## Remaining limitations

- Fake-TLS (`ee`) cannot connect. This is the most likely reason your proxy does not work with cppgram, and this change
  does not fix it.
- Base64 strictness is not checked against TDLib's decoder source, because that source is not in the local copy.
- Raw binary secrets (TDLib also accepts them) are not accepted. The configuration only carries text.
- The parsed key stays in memory until the object is destroyed (P5).
- The live proxy test was not run against a server. No claim of live compatibility is made.

## VPS run: padded plain replies

Root cause, from the VPS log: the external proxy accepted the handshake and relayed a reply, so the secret was correct.
The client then failed with `plain packet length mismatch` in `unwrap_plain_message`. A padded-intermediate reply carries
0..15 padding bytes after the packet. The transport passed them up, and the plain decoder required the length field to
match the byte count exactly. The encrypted path already ignores trailing bytes.

Fix: the length field is authoritative. Fewer bytes than it claims is still an error. Up to 15 trailing bytes are
ignored. More than 15 is rejected. The error messages now include the byte counts.

Verified offline: 4 new tests, one of them written red first, then green. The full suite passes (437 mtproto, 691 CTest).
Not verified: a live run on the VPS. Rebuild there and rerun `mtproto_proxy`.

Local proxy at 127.0.0.1:8000: the client connected and then got silence for 10 seconds. The official MTProxy relays
replies from Telegram, so silence usually means it has no working path to a DC. Check its log, its config files, and
whether the VPS can reach a Telegram DC on port 443. This change does not affect that case.
