# Reference comparison: CppGram, gotdbot and TDLib

This compares CppGram 0.1.0 with two references that were supplied with the task. It records what each project does and
what CppGram does differently. It was done by reading source, and the notes say where a claim was checked by a test or
not checked at all.

## What each reference is

| Project | Language | What it is | Independent MTProto? | License |
|---|---|---|---|---|
| CppGram 0.1.0 (`cppgram-main_1_5.zip`) | C++20 | Own MTProto client, own obfuscated transport, own TL layer | Yes | MIT (tgbot-cpp derived parts), see `NOTICE` |
| gotdbot (`gotdbot-master.zip`) | Go 1.26 | Go API over TDLib's `tdjson` shared library, loaded with purego | No. All protocol work is in TDLib | MIT |
| TDLib (`td-master.zip`) | C++ | Official Telegram library. Full MTProto client with DC, auth, session and proxy logic | Yes, the reference implementation | Boost Software License 1.0 |

gotdbot has no MTProto code of its own. `internal/tdjson` loads TDLib and passes JSON requests to it. So gotdbot is useful
for API design, not for protocol behaviour. No code was copied from any reference.

## Comparison by area

Status column: **Fixed** (changed in this pass, with a test), **Checked** (read and compared, no change), **Gap** (known,
not changed), **Not audited** (not reviewed in this pass).

| Area | CppGram | gotdbot | TDLib | Status and action |
|---|---|---|---|---|
| Bot login and migration | `login_bot` sent `auth.importBotAuthorization` once on the configured DC. A `USER_MIGRATE_N` reply was thrown as RPC 303 | Not applicable. TDLib's `setAuthenticationBotToken` handles it | Migration is handled centrally (see next row) | **Fixed.** The request is repeated on DC N with a new key, bounded to two hops. Tests: `DcMigrationFakeDc.BotLoginFollowsUserMigrateToHomeDc`, `MigrationLoopIsBounded` |
| Migration of any request | Only in the new bot login path | Not applicable | `NetQueryDispatcher::try_fix_migrate` (`td/telegram/net/NetQueryDispatcher.cpp`, around line 391) handles `PHONE_MIGRATE_`, `NETWORK_MIGRATE_` and `USER_MIGRATE_` for every query. It updates the main DC for the whole client and resends the query. It also handles `FILE_MIGRATE_N` by resending to that DC | **Fixed for the login steps.** `send_login_code`, `sign_in_code`, `sign_in_password` and `resend_login_code` follow `PHONE_MIGRATE_N`. The pending code and 2FA challenge survive a migration caused by another request, and the code is submitted to the DC that issued it. **Gap:** other RPCs are still not retried. A connection drop on the same DC still clears a pending code. See `KNOWN_LIMITATIONS.md` items 62 and 63. Tests: `PhoneMigrationFakeDc.*`, `AuthMigrationState.*` |
| DC address selection | Options (`dc_ip`, `dc_port`), then `help.getConfig`, then the built-in table. Before this pass a saved session was dialled on the options DC | Not applicable | Uses the DC option list from config and the auth key's DC | **Fixed.** A saved session is loaded first and its DC is dialled. `DcMigrationFakeDc.RestartDialsTheDcOfTheSavedSession` |
| Migrate error parsing | `migrate_dc_from_error` accepted DCs 1 to 5 only | Not applicable | `to_integer<int32>` and `DcId::is_valid` | **Fixed.** Accepts 1 to 99. Test `DcMigrationParse.ReadsDcNumberFromMigrateErrorsOnly` |
| Key per DC | One session file holds one key and one `dc_id` | Not applicable | Keys are kept per DC | **Checked.** A forced migration discards the old key for the new DC. The old file is replaced only after the new handshake succeeds |
| Obfuscated transport | Own implementation | Not applicable | Own implementation, with transport options | **Checked** in an earlier pass with a separate Python reference. Init layout, key and IV derivation, tag and DC bytes, and stream continuation matched byte for byte. That check is scratch-only and not in the archive |
| Session storage | File, SQLite and session string, optionally encrypted | Stored by TDLib's database directory | TDLib database | **Not audited** in this pass beyond the restore path that the migration change touched |
| FLOOD_WAIT | Retry with a policy, a maximum wait and an attempt limit, with fake-DC tests | Not applicable | Checks code 420 and the `FLOOD_WAIT`, `STORY_SEND_FLOOD` family in `NetQueryDispatcher.cpp` around line 104 | **Not audited** in this pass. The existing `FloodWait*` tests passed (see `TEST_RESULTS.md`) |
| Reconnect | Bounded reconnect budget with fake-DC tests (`ClientFakeDc.*`) | Not applicable | Connection supervision in the net layer | **Not audited** in this pass. Existing tests passed |
| MTProto proxy and SOCKS5 | Both implemented, with tests. Fake-TLS (`ee`) rejected | Not applicable | Proxy support in `td/telegram/net` | **Not audited** in this pass. Existing tests passed. Live proxy not run |
| Updates | Handler registration, gate and `pts` tests | Typed Go handlers over TDLib updates | TDLib update queue | **Not audited** in this pass |
| Async and cancellation | `async::AsyncResult`, `Client::async`, cancel and stress tests | Not applicable | Actor model | **Not audited** in this pass. Existing async and cancel tests passed |
| Error handling | Typed error hierarchy in `errors.h`. `mtproto::RpcError` carries the numeric code | Errors come back from TDLib as JSON | Status objects | **Gap.** The public API throws `mtproto::RpcError` for raw RPC errors and `errors::` types elsewhere. Documented in `docs/API.md`. Not unified in this pass |
| API design | Typed C++ methods, documented in `docs/API.md` and `docs/API_INDEX.md` | Typed Go methods generated from the TDLib schema | Raw TL and JSON | Not compared in depth |

## Practical differences that matter for the reported error

1. **TDLib is not a single-DC client.** When any query gets `USER_MIGRATE_N`, TDLib moves the main DC and retries the
   query. CppGram now does this for bot login and for the phone login steps (`send_login_code`, `sign_in_code`,
   `resend_login_code`). Other RPCs are not retried. The `RPC error 303: USER_MIGRATE_5` in the report came from the bot
   path. Its fix is in the change set, and the regression tests fail without it.
2. **A saved session must dial its own DC.** TDLib keeps the DC with the key. CppGram used to dial the options DC and
   then load a key that belonged to another DC.
3. **Migration must not reuse a key.** Both TDLib and the fix create a new key per DC. Reusing DC 2's key on DC 5 fails
   without a useful error.

## What is not claimed

- Behaviour for the sign-in state machine beyond the migration path, for FLOOD_WAIT, for reconnection, for proxy and SOCKS5, and for updates was not re-audited in
  this pass. The statements above are from reading the code and from the existing tests.
- No live Telegram test was run. A migrate reply from the real servers was not observed. The fake DC replies are written
  from the TL schema, so they show that CppGram reacts to the reply it expects, not that the real server sends it.

## Proxy and transport (this phase)

These rows come from reading the TDLib source in `td-master` (not from running TDLib) and comparing it with this tree.

| Area | CppGram | TDLib (source read) | Status |
|---|---|---|---|
| Intermediate and padded tags | `0xeeeeeeee` and `0xdddddddd` in the handshake tag | Same values, `td/mtproto/TcpTransport.cpp` lines 76 and 109 | **Checked.** Same values, and the tag position matches the handshake layout (bytes 56..60) |
| Abridged tag | `0xefefefef`, abridged framing | Not found in the TCP or obfuscated transport code read | **Gap, not a difference in behaviour that was tested.** Abridged was added here from the published tag. TDLib's proxy path does not show it, so no comparison was possible |
| DC id field | Always written at bytes 60..62 | Written only when `dc_id_` is non-zero, `TcpTransport.cpp` lines 110 and 111 | **Recorded difference** (P6 in `KNOWN_LIMITATIONS.md`). Same result for normal use |
| Fake-TLS (`ee`) | Refused before the network | No fake-TLS transport found in `td/telegram` or `td/mtproto` in this source | **Consistent:** neither side implements it in that layer. TDLib's `from_binary` does accept the `0xee` form and keeps it as a secret |
| Secret length rules | Plain 16 bytes, padded 17 bytes with `0xdd` (as 34 hex digits) | `ProxySecret::from_binary` (`td/mtproto/ProxySecret.cpp` lines 37 to 45): 16 bytes, 17 with `0xdd`, 18 or more with `0xee` | **Same rules for plain and padded.** TDLib also accepts base64url and base64 in `from_link`, and raw binary. CppGram accepts hex only (P4) |
| Secret truncation | Not done. Too long is refused | `from_link(..., truncate_if_needed)` can truncate | **Difference.** Not added. Refusing is the stricter behaviour |
| Proxy secret in logs | Not logged, tested for error text | Not checked in this phase | Not compared |
