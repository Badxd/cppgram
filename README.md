# cppgram

C++20 library for Telegram. It has two parts:

- **MTProto client** (`cppgram::Client`): logs in as a bot or a user and talks to Telegram directly.
- **Bot API layer** (`cppgram::bot`): HTTP Bot API wrapper, derived from [tgbot-cpp](https://github.com/reo7sp/tgbot-cpp).

Repository: [Badmunda05/cppgram](https://github.com/Badmunda05/cppgram)

## Status

- [x] Tests: complete
- [x] Live tests: complete
- [x] MTProto client: login, messages, chats, users, bots, media, keyboards, polls, stickers, forums, folders, invites, privacy, sessions
- [x] Updates: pts/qts/seq gap detection, getDifference, handler groups
- [x] Storage: SQLite or binary session file, session strings
- [x] Parser: HTML and MarkdownV2 entities, CDN downloads
- [x] Bot API layer
- [ ] Stories, payments and Stars, business, gifts, boosts, wallet/TON

## Example

Echo bot with the MTProto client:

```cpp
#include <iostream>

#include "cppgram/client.h"

int main() {
    cppgram::ClientOptions opt;
    opt.api_id = 12345;                 // from https://my.telegram.org
    opt.api_hash = "your_api_hash";
    opt.session_path = "bot.sqlite";

    cppgram::Client client(opt);
    client.connect();
    client.login_bot("PLACE YOUR BOT TOKEN HERE");
    std::cout << "logged in as " << client.get_me().username << "\n";

    client.on_message(cppgram::filters::text(), [](cppgram::Client& c, const cppgram::Message& m) {
        c.send_text(m.chat, "You said: " + m.text);
    });

    client.run();
    return 0;
}
```

More examples are in [`examples`](examples), including Bot API ones.

## Build

```sh
make build              # configure and build the library and tests (Ninja when installed)
make test               # run the gtest suites
make live               # build only the live binaries, in build/live
make live-offline-check # offline live-test checks: no network, no credentials
make tl-generate        # regenerate the TL code from schema/*.tl
make install INSTALL_PREFIX=build/install
```

Requirements: CMake 3.16+, a C++20 compiler (GCC 12+ or Clang 18+), OpenSSL, zlib, SQLite 3, libcurl, Boost (headers),
nlohmann/json, and GoogleTest for the tests. On Debian or Ubuntu:

```sh
sudo apt-get install -y build-essential cmake libssl-dev zlib1g-dev libsqlite3-dev \
    libcurl4-openssl-dev libboost-dev nlohmann-json3-dev libgtest-dev
```

Build options:

| Option | Default | Effect |
|---|---|---|
| `ENABLE_TESTS` | `OFF` | Build the test suites |
| `BUILD_LIVE_CHECK` / `BUILD_LIVE_TESTS` | `OFF` | Build the live binaries |
| `BUILD_SHARED_LIBS` | `OFF` | Shared library |
| `ENABLE_SANITIZERS` / `ENABLE_COVERAGE` | `OFF` | ASan and UBSan, or gcov coverage |
| `CPPGRAM_USE_CCACHE` | `ON` | Use a compiler cache when one is installed |
| `CPPGRAM_COMPILER_CACHE` | `auto` | `auto`, `ccache`, `sccache` or `off` |
| `CPPGRAM_FAST_LINKER` | `auto` | `auto` (mold, then lld), `mold`, `lld` or `off` |
| `CPPGRAM_UNITY_BUILD` | `OFF` | Merge sources into batches: faster clean builds |

Other checks: `bash tools/check_headers.sh`, `bash tools/check_examples.sh`, `bash tools/run_sanitizers.sh`,
`bash tools/run_tsan.sh`. None of them contact Telegram.

## Errors, logging and Web Apps

- `cppgram/errors.h`: typed errors (`errors::Error` and subclasses). `errors::categorize(e)` works for all errors of this library, old and new.
- `cppgram/log.h`: structured logging with levels, components, request ids, sinks and secret redaction. Default level is WARN.
- `cppgram/webapp.h`: verify Web App `initData`. Returns parsed data only when the signature and time checks pass.

## Usage in CMake

```cmake
find_package(cppgram CONFIG REQUIRED)
target_link_libraries(your_bot PRIVATE cppgram::cppgram)
```

Or add the repository with `add_subdirectory`.

## Live tests

`cppgram_live_check` runs a real connection: TCP, auth key handshake, `initConnection`, `login_bot` and `get_me`.
`cppgram_live_tests` runs six tests: reconnect, flood_wait, socks5, session_restore, mtproto_proxy and padded_framing.
Credentials come only from environment variables. Details: [LIVE_CHECK.md](LIVE_CHECK.md) and
[guides/LIVE_TESTS.md](guides/LIVE_TESTS.md).

## Layout

- `include/cppgram/client.h`: MTProto client API
- `include/cppgram/types.h`: typed wrappers (User, Chat, Message, Poll, Sticker, ...)
- `include/cppgram/bot.h`, `include/cppgram/bot/`: Bot API layer
- `include/cppgram/mtproto/`: crypto, TL, transports, auth, connection, session, SRP
- `src/`: client implementation, one file per area (for example `send.cpp`, `chats.cpp`, `stickers.cpp`)
- `src/mtproto/schema/`: generated schema code
- `tests/`: `mtproto/` suites for the client, `bot/` suites for the Bot API layer
- `examples/`: example programs

## Docs

- [guides/](guides/README.md): installation, authentication, sessions, architecture, API matrix, threat model
- [guides/PRODUCTION_READINESS.md](guides/PRODUCTION_READINESS.md): what is tested and what is verified live
- [guides/RECONNECT_AND_FLOOD_WAIT.md](guides/RECONNECT_AND_FLOOD_WAIT.md), [guides/PROXIES.md](guides/PROXIES.md),
  [guides/SESSION_ENCRYPTION.md](guides/SESSION_ENCRYPTION.md)
- [HOW_TO_UPDATE_TELEGRAM_API.md](HOW_TO_UPDATE_TELEGRAM_API.md): updating the schema

## Licence

MIT for the tgbot-cpp-derived parts, see [LICENSE](LICENSE). The MTProto schema derives from Kurigram (LGPL-3.0). See
[NOTICE](NOTICE).
