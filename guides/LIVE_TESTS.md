# Live Telegram tests

`cppgram_live_tests` runs six tests against real Telegram servers, or against the proxy you configure. Each test is
independent and runs on its own. Nothing is faked: a test reports PASS only when the real call succeeded.

Offline protocol and unit tests are a separate suite (`cppgram_mtproto_test`, `cppgram_bot_test`). They never
touch the network and are not part of this runner.

## Status labels

| Label | Meaning |
|---|---|
| PASS | The real check ran and succeeded. |
| FAIL | The real check ran and failed. The detail line says which stage failed. |
| SKIP | A prerequisite is missing (credentials, proxy settings, passphrase, or a suitable secret). Nothing was tested. |
| NOT_SUPPORTED | The configuration asks for something cppgram does not implement (fake-TLS `ee` proxy secrets). |

Exit codes: `0` PASS, `1` FAIL, `2` usage error, `3` SKIP, `4` NOT_SUPPORTED. With `all`, the exit code is `1` if any
test failed, otherwise `0`.

## The six tests

| Test name | What it checks | Needs |
|---|---|---|
| `reconnect` | A real network drop and automatic reconnect while `run()` is active. | Credentials |
| `flood_wait` | FLOOD_WAIT handling. Only passes if Telegram really sends one. | Credentials |
| `socks5` | Connection to Telegram through a SOCKS5 proxy. | Credentials, SOCKS5 proxy |
| `session_restore` | Encrypted session saved, wrong passphrase refused, restore reuses the same auth key. | Credentials, `CPPGRAM_SESSION_PASSPHRASE` |
| `mtproto_proxy` | Connection through an obfuscated MTProto proxy (plain or `dd` secret). | Credentials, MTProto proxy |
| `padded_framing` | Same as above, but requires a `dd` (padded-intermediate) secret. | Credentials, `dd` MTProto proxy |

### How each test works

- **reconnect**: the client connects to Telegram through a local relay on `127.0.0.1` that forwards bytes unchanged.
  After `run()` starts, the test closes the relay for 4 seconds, which drops the connection for real, then reopens it.
  PASS needs a `Reconnecting` event, a second `Connected` event within 90 seconds, and a successful `get_me` after
  `stop()`. The relay is not a Telegram mock: Telegram sees a normal MTProto session.
- **flood_wait**: makes only ordinary calls (`login_bot`, `get_me`). It does not send extra requests to provoke a limit.
  If no FLOOD_WAIT happens, the result is SKIP, not PASS. Unit tests cover the retry policy offline.
- **socks5**: `TG_SOCKS5_HOST` and `TG_SOCKS5_PORT` are required. User and password are optional and must be set
  together.
- **session_restore**: run 1 logs in and saves the session. The file must be encrypted (`CGE1`). A wrong passphrase
  must throw `SessionCorruptError`. Run 2 is a new `Client` that loads the same file and must keep the same auth key
  (no new handshake). Then `get_me` must work.
- **mtproto_proxy**: the secret is validated first. `ee` gives NOT_SUPPORTED without any network use. Plain (32 hex)
  and `dd` secrets run the connection.
- **padded_framing**: needs a `dd` secret, otherwise SKIP. PASS shows that the client connects and completes
  `get_me` through the proxy using padded-intermediate framing. It does not prove the server pads its frames.
  Padding handling on decryption is covered by offline tests.

## Environment variables

Credentials come only from the environment. Never put them in a file in this repository.

| Variable | Used by | Notes |
|---|---|---|
| `TG_API_ID` | all Telegram tests | Number from https://my.telegram.org |
| `TG_API_HASH` | all Telegram tests | Never printed |
| `TG_BOT_TOKEN` | all Telegram tests | Bot token from @BotFather, used by `login_bot`. Never printed |
| `TG_DC` | all Telegram tests | 1 to 5. Default `2` |
| `TG_LIVE_DIR` | all | Directory for `live-*.session` files. Default `.` |
| `TG_DEBUG` | all | `1` prints library handshake stages. Default off |
| `TG_SOCKS5_HOST`, `TG_SOCKS5_PORT` | `socks5` | Required for that test |
| `TG_SOCKS5_USER`, `TG_SOCKS5_PASS` | `socks5` | Optional. Set both or neither. Never printed |
| `TG_MTPROTO_PROXY_HOST`, `TG_MTPROTO_PROXY_PORT` | `mtproto_proxy`, `padded_framing` | Proxy you are allowed to use |
| `TG_MTPROTO_PROXY_SECRET` | `mtproto_proxy`, `padded_framing` | Plain (32 hex) or `dd` + 32 hex. Never printed |
| `CPPGRAM_SESSION_PASSPHRASE` | `session_restore` | Passphrase for the encrypted session. Never printed |

## Building

Only the runner and the library are built. The fuzzers and the test suite are not.

    cmake -S . -B build/live -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_LIVE_TESTS=ON
    cmake --build build/live --target cppgram_live_tests

Ninja builds in parallel. If `ccache` is installed, it is used automatically (`CPPGRAM_USE_CCACHE=OFF` turns it off).
Release is the CMake default.

## Running

    cd build/live
    export TG_API_ID=... TG_API_HASH=... TG_BOT_TOKEN=...
    ./cppgram_live_tests reconnect
    ./cppgram_live_tests flood_wait
    export TG_SOCKS5_HOST=... TG_SOCKS5_PORT=...   # your proxy
    ./cppgram_live_tests socks5
    export CPPGRAM_SESSION_PASSPHRASE=...           # any passphrase you choose
    ./cppgram_live_tests session_restore
    export TG_MTPROTO_PROXY_HOST=... TG_MTPROTO_PROXY_PORT=... TG_MTPROTO_PROXY_SECRET=...
    ./cppgram_live_tests mtproto_proxy
    ./cppgram_live_tests padded_framing

Run one test at a time. Each one writes its own `live-*.session` file, which is created fresh in `TG_LIVE_DIR`.
Do not run `all` against a real account unless you intend every test to run in one go.


## Offline checks for the runner

`tools/check_live_tests_offline.sh <binary>` runs the runner without credentials and checks the SKIP, NOT_SUPPORTED,
FAIL-on-bad-secret and usage paths. It does not touch the network.

## Limits

- The six tests have not been run against live Telegram from this repository's CI. Each result must come from a real
  run, and the run must be recorded with its date, DC and proxy type.
- fake-TLS (`ee`) proxy secrets are not supported and are reported as NOT_SUPPORTED.
- `flood_wait` cannot pass unless Telegram sends a FLOOD_WAIT naturally, and the runner does not provoke one.
- `reconnect` drops a relay connection, not a server-side connection. It shows the client reconnects after a real
  network loss, not that Telegram behaves a particular way.
- The bot account used in the tests must be allowed to log in. Do not use a production bot's token.
- Test sessions are unencrypted unless `CPPGRAM_SESSION_PASSPHRASE` is set, and the non-restore tests do not set it.
