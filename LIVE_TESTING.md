# Live testing CppGram

CppGram's offline tests (`ctest`) never touch the network. Live tests check the library against real Telegram servers,
or against a proxy you configure. Live results are only meaningful when you run them yourself, with your own account
or bot. This repository has no credentials, and no live result for this release has been recorded (see
`TEST_RESULTS.md`).

There are two ways to run live tests.

| Approach | Project | What it covers | Where it runs |
|---|---|---|---|
| 1. Direct | this repository, targets `cppgram_live_check` and `cppgram_live_tests` | six tests (`reconnect`, `flood_wait`, `socks5`, `session_restore`, `mtproto_proxy`, `padded_framing`) plus one connection check | Your machine. Environment variables. Run by hand |
| 2. Bot-controlled (separate project) | **CppGramLiveTest, separate live-testing project: [github.com/Badxd/Cpplivetest](https://github.com/Badxd/Cpplivetest)** | A separate MTProto bot that uses CppGram. Eight test commands, run from Telegram chat by an admin | A host you control (a VPS or your PC) |

The CppGramLiveTest project (repository `Badxd/Cpplivetest`) is a separate live-testing project. It does not contain CppGram source. It builds against a CppGram tree
or a prebuilt `libcppgram.a`, and it does not change the library. Other developers can use it to run extra live checks of
CppGram, including checks that only exist as bot commands.

## Approach 1: direct live tests

### Build

```sh
cmake -S . -B build/live -DCMAKE_BUILD_TYPE=Release -DBUILD_LIVE_CHECK=ON -DBUILD_LIVE_TESTS=ON
cmake --build build/live --target cppgram_live_check cppgram_live_tests --parallel 2
```

`make live` does the same. `make live-offline-check` builds them and checks, without network and without credentials,
that every Telegram test reports SKIP or NOT_SUPPORTED. Use it to confirm the wiring before you add credentials.

### Environment variables (names only; set the values yourself)

| Variable | Used by | Notes |
|---|---|---|
| `TG_API_ID`, `TG_API_HASH` | all Telegram tests | From https://my.telegram.org. The hash is never printed |
| `TG_BOT_TOKEN` | all Telegram tests | Token of a dedicated test bot from @BotFather. Used by `login_bot`. Never printed |
| `TG_DC` | all Telegram tests | 1 to 5, default 2 |
| `TG_DCS` | `cppgram_live_check` | Order of DCs to try, default `2,1,3,4,5` |
| `TG_LIVE_DIR` | `cppgram_live_tests` | Directory for session files written by the tests |
| `TG_DEBUG` | both | `0` turns off stage logs |
| `TG_SOCKS5_HOST`, `TG_SOCKS5_PORT` | `socks5` | Required together. `TG_SOCKS5_USER`, `TG_SOCKS5_PASS` optional, but set together |
| `TG_MTPROTO_PROXY_HOST`, `TG_MTPROTO_PROXY_PORT`, `TG_MTPROTO_PROXY_SECRET` | `mtproto_proxy`, `padded_framing` | Plain 32-hex secret, or `dd` secret for `padded_framing` |
| `CPPGRAM_SESSION_PASSPHRASE` | `session_restore` | Passphrase for the encrypted session file |

### Run

```sh
./build/live/cppgram_live_tests reconnect
./build/live/cppgram_live_tests all
```

`guides/LIVE_TESTS.md` is the full guide: what each test proves, its prerequisites, and its limits.

### Which tests need direct execution

Direct execution is the only way to run `reconnect` (it drops the connection for real through a local relay while `run()`
is active), `socks5`, `session_restore` (it needs a passphrase and two runs), and `padded_framing`. The bot does not run
these, although it covers the `dd` proxy path with `/test proxy-dd`.

## Approach 2: bot-controlled live tests (CppGramLiveTest, separate project)

Repository (separate live-testing project, CppGramLiveTest): **https://github.com/Badxd/Cpplivetest**

What it does: a systemd-friendly bot that accepts commands only from admin user ids and runs one test at a time with its
own CppGram client. Results are sent to the chat with `/report` and written to `results.jsonl`.

Commands (from its `COMMANDS.md`):

| Command | Test | Type |
|---|---|---|
| `/test direct` | TCP connect and handshake to the configured DC. No login | Live |
| `/test login` | Bot login and `get_me`, id checked against startup | Live |
| `/test session` | Export a session string, store it in a 0600 file, restore into a fresh client, check identity | Live |
| `/test reconnect` | Login, controlled disconnect, connect again, `get_me` | Live (controlled, not the library's `run()` drop) |
| `/test flood` | Offline mock of FLOOD_WAIT classification and the wait limit. Sends no request. Passively reports a real FLOOD_WAIT if one happens | Offline mock + passive |
| `/test message` | Sends a controlled message to the admin chat and reads it back | Live. Run it separately |
| `/test proxy-plain` | Plain 16-byte secret proxy profile: connect, login, `get_me`, reconnect | Live, needs a proxy |
| `/test proxy-dd` | `dd` padded-intermediate proxy profile | Live, needs a proxy |
| `/test all` | Runs the list in order (`message` is SKIP there) | Mixed |

Not covered by the bot: `socks5`, the library's own `run()` network-drop recovery, long soak runs, and systemd restart
behaviour. Use Approach 1 for those.

### Build the bot

Dependencies (Ubuntu 24.04): `build-essential cmake libboost-dev libcurl4-openssl-dev nlohmann-json3-dev libssl-dev zlib1g-dev
libsqlite3-dev`.

```sh
git clone https://github.com/Badxd/Cpplivetest
cd Cpplivetest
# Either build the library from an unmodified CppGram tree...
CPPGRAM_SOURCE_DIR=/path/to/cppgram ./build.sh
# ...or link an already built libcppgram.a:
CPPGRAM_PREBUILT_DIR=/path/to/cppgram/build CPPGRAM_PREBUILT_INCLUDE_DIR=/path/to/cppgram/include ./build.sh
```

`build.sh` runs the bot's offline unit tests. The binary is `build/cppgram_live_test_bot`. The bot's `README.md` covers
deployment (`deploy/`), which is optional.

### Bot environment variables (names only)

`TG_API_ID`, `TG_API_HASH`, `TG_BOT_TOKEN` (a dedicated test bot), `TG_ADMIN_IDS` (comma-separated admin user ids),
`TG_DC`, `TG_DATA_DIR`, `TG_LOG_DIR`, `TG_FLOOD_MAX_WAIT_SECONDS`, `TG_PROXY_PLAIN_HOST`, `TG_PROXY_PLAIN_PORT`,
`TG_PROXY_PLAIN_SECRET`, `TG_PROXY_DD_HOST`, `TG_PROXY_DD_PORT`, `TG_PROXY_DD_SECRET`, `TG_DEBUG`. Proxy profiles are all-or-none.
Fake-TLS (`ee`) secrets are rejected by CppGram and by the bot.

Use your own Telegram test account or bot. Do not use a production account.

## Reading results

| Label | Meaning |
|---|---|
| PASS | The real check ran and succeeded |
| FAIL | The real check ran and failed. The detail names the stage that failed |
| BLOCKED | Cannot run: a credential, a proxy endpoint or an environment is missing |
| SKIP | A prerequisite is missing, or the test is not run in this context. Nothing was tested |
| NOT_SUPPORTED | The configuration asks for something CppGram does not implement (fake-TLS `ee` secrets) |
| NOT OBSERVED | An event you were watching for did not happen (for example a live FLOOD_WAIT). Not a pass |

Direct tool exit codes: `0` PASS, `1` FAIL, `2` usage error, `3` SKIP, `4` NOT_SUPPORTED. With `all`, the exit code is `1`
if any test failed.

## Reporting a failure

Open an issue in the repository that owns the test (CppGram for `cppgram_live_*`, CppGramLiveTest for bot tests). Include:

- The test name and its label, and the stage named in the detail line.
- The library commit you built (`git rev-parse HEAD`) and your compiler version.
- The library error text, including any byte counts such as `plain packet has N bytes`. Those numbers matter.
- The DC number and the proxy type (plain or `dd`). Do not include the proxy host, the secret or the session file.

Never post API hashes, bot tokens, session strings, passphrases, proxy secrets or phone numbers in an issue or in a chat.
If you pasted one by mistake, revoke or rotate it at once.

## Related documents

- [`guides/LIVE_TESTS.md`](guides/LIVE_TESTS.md): the direct test guide in full
- [`LIVE_CHECK.md`](LIVE_CHECK.md) and [`CPPGRAM_LIVE_TEST_GUIDE.md`](CPPGRAM_LIVE_TEST_GUIDE.md): earlier live-check notes
- [`VPS_TESTING_GUIDE.md`](VPS_TESTING_GUIDE.md): the owner's VPS test plan
- [`PROXY_AUDIT.md`](PROXY_AUDIT.md): MTProxy framing audit, including the plain trailing-byte decision
