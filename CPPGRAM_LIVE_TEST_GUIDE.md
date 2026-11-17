# Live test guide (owner runs these, not the release-candidate build tests)

Nothing in this document was run. Every live test is NOT RUN until the owner runs it and records the output.

## Build the live runner

```
cmake -S . -B build/live -DCMAKE_BUILD_TYPE=Release -DBUILD_LIVE_TESTS=ON
cmake --build build/live --target cppgram_live_tests --parallel 1
```

Use `--parallel 1` on a small VPS (see the build memory report).

## Environment variables (names only; set them in your shell, never in files in the archive)

| Name | Used by |
|---|---|
| `TG_API_ID` | all tests |
| `TG_API_HASH` | all tests |
| `TG_BOT_TOKEN` | bot-token login tests |
| `CPPGRAM_SESSION_PASSPHRASE` | encrypted session tests (read by the runner) |
| `TG_TARGET` | message send tests (the target chat) |

The runner also reads the proxy secret and SOCKS5 password from its own environment variables. Read the header comment
in `examples/live_tests/live_tests.cpp` for their exact names before running `socks5` or `mtproto_proxy`.

## Commands and expected output

Run one test at a time: `./build/live/cppgram_live_tests <test>`. Use `all` only when you are ready for every test.

| Test | Command | Expected | Exit code |
|---|---|---|---|
| reconnect | `./build/live/cppgram_live_tests reconnect` | connects, drops, reconnects, prints PASS | 0 PASS, 1 FAIL, 3 SKIP |
| flood_wait | `./build/live/cppgram_live_tests flood_wait` | PASS only if Telegram returns FLOOD_WAIT naturally; otherwise SKIP. Never provoke it | 0 / 3 |
| socks5 | `./build/live/cppgram_live_tests socks5` | connects through the configured SOCKS5 proxy | 0 / 1 |
| session_restore | `./build/live/cppgram_live_tests session_restore` | restores an encrypted session file | 0 / 1 |
| mtproto_proxy | `./build/live/cppgram_live_tests mtproto_proxy` | connects through an MTProto proxy (plain or padded secret) | 0 / 1 / 4 |
| padded_framing | `./build/live/cppgram_live_tests padded_framing` | connects with padded-intermediate framing | 0 / 1 |

Exit codes come from the runner's own header: 0 PASS, 1 FAIL, 2 usage error, 3 SKIP, 4 NOT_SUPPORTED.

## Not in the runner (add a test before claiming these)

- Phone login, `get_me`, and two-step verification: no runner test exists. Test them by hand with a throwaway account.
- QR login (`export_login_token`, `import_login_token`), authorization export and import, stories methods, Stars, gifts,
  CDN download and reupload: no runner test exists.

## Failure diagnosis

- Exit 2: wrong arguments. Check the test name against the table above.
- Exit 1 with "set TG_API_ID and TG_API_HASH": the variables are missing or empty.
- Connection timeouts: check the DC address and firewall. Try `padded_framing` first (plain transport).
- FLOOD_WAIT with a long wait: stop, wait the time Telegram gives, and do not retry in a loop.

## Cleanup

- Delete the session file after the test: `rm -f <session_path>`. It contains auth keys.
- Unset the variables: `unset TG_API_HASH TG_BOT_TOKEN CPPGRAM_SESSION_PASSPHRASE`.
- Never commit session files, API hashes, bot tokens or passphrases.
