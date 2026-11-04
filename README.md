# cppgram

[![Group](https://img.shields.io/badge/group-PBXCHATS-blue)](https://t.me/PBXCHATS)
[![Updates](https://img.shields.io/badge/updates-PBX__UPDATE-blue)](https://t.me/PBX_UPDATE)

C++20 library for Telegram. It has two layers:

- **MTProto client** (`cppgram::Client`): talks to Telegram directly, logs in as a bot or user, and covers the core
  Kurigram surface for messages, chats, users, bots, media, keyboards, polls, stickers, forum topics, folders, invite
  links, privacy, sessions and two-step verification.
- **Bot API layer** (`cppgram::bot`): the HTTP Bot API wrapper derived from tgbot-cpp.

Maintainer: [Badmunda05](https://github.com/Badmunda05) · Owner: [BadMundaXD](https://t.me/BadMundaXD) ·
Contact: mumda.bad1322@gmail.com · Repository: [Badmunda05/cppgram](https://github.com/Badmunda05/cppgram)

## Status

| Part | State |
|---|---|
| Crypto: SHA1/SHA256, CRC32, AES-IGE, AES-CTR, MTProto 2.0 packets, RSA, DH, pq factorisation | done, tested |
| TL serialisation and generated schema (layer 232: 1754 types, 869 functions) | done, tested |
| Transports: intermediate, abridged, full | done, tested |
| Auth key handshake and encrypted session (acks, bad salt retry, gzip) | done, tested against an in-process fake server |
| Client: login (bot, 2FA SRP), messages, chats, users, bots, media, keyboards, polls, stickers, forum topics, folders, invite links, join requests, privacy, sessions, favourite stickers | done, offline tested, **not live-tested** |
| Updates: pts/qts/seq gap detection, getDifference, handler groups, error hook | done, offline tested, **not live-tested** |
| Storage: SQLite session and update state, or a binary session file | done, tested |
| Session strings (export and restore a login as one line) | done, offline tested, **not live-tested** |
| Parser: HTML and MarkdownV2 into entities with UTF-16 offsets | done, tested |
| CDN downloads (AES-256-CTR, per-part SHA-256 check) | done, **not live-tested** |
| Bot API layer (`cppgram::bot`) | done, tested |
| Stories, payments and Stars, business, gifts, boosts, wallet/TON | not started |

"Not live-tested" means the code is checked by offline tests and a fake server only. Run `cppgram_live_check` (see
[LIVE_CHECK.md](LIVE_CHECK.md)) against Telegram before relying on it.

## Example

Echo bot with the MTProto client:

```cpp
#include <iostream>

#include "cppgram/client.h"

int main() {
    cppgram::ClientOptions opt;
    opt.api_id = 12345;                 // from https://my.telegram.org
    opt.api_hash = "your_api_hash";
    opt.session_path = "bot.sqlite";    // .sqlite/.db stores the session and update state in SQLite

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

Handlers in a lower `group` run first. Inside one group every matching handler runs. Use `client.on_error(...)` to
receive handler exceptions instead of printing them to stderr.

Bot API layer, for HTTP-only bots:

```cpp
#include <iostream>
#include <memory>

#include "cppgram/bot.h"

int main() {
    cppgram::bot::Bot bot("PLACE YOUR TOKEN HERE");
    bot.getEvents().onAnyMessage([&bot](std::shared_ptr<cppgram::bot::Message> message) {
        bot.getApi().sendMessage(message->chat->id, "Your message is: " + message->text.value_or(""));
    });
    try {
        cppgram::bot::TgLongPoll longPoll(bot);
        longPoll.startLoop();
    } catch (const cppgram::bot::TgException& e) {
        std::cout << "error: " << e.what() << std::endl;
    }
    return 0;
}
```

## Documentation

Production readiness status (what is offline tested, locally verified, or not verified live): [guides/PRODUCTION_READINESS.md](guides/PRODUCTION_READINESS.md).
Reconnect and FLOOD_WAIT: [guides/RECONNECT_AND_FLOOD_WAIT.md](guides/RECONNECT_AND_FLOOD_WAIT.md).
SOCKS5 and MTProto proxy: [guides/PROXIES.md](guides/PROXIES.md).
Encrypted sessions: [guides/SESSION_ENCRYPTION.md](guides/SESSION_ENCRYPTION.md).


See [guides/](guides/README.md) for installation, authentication, sessions, architecture, the API support
matrix, operations, the threat model, benchmarks and migration notes.

## Build

```sh
make build              # configure and build the library and tests (Ninja when installed)
make test               # run the gtest suites
make live               # build only the live binaries, in build/live
make live-offline-check # offline live-test checks: no network, no credentials
make tl-generate        # regenerate the TL code from schema/*.tl (C++ tool, no Python)
make install INSTALL_PREFIX=build/install
```

Build speed: the Makefile uses Ninja when it is installed, configures each build directory once, and uses a compiler
cache (ccache or sccache) and a fast linker (mold, then lld) when they are present. Options are listed under
"CMake options" below. `make reconfigure` forces a fresh configure.

Dependencies: CMake 3.16+, a C++20 compiler (verified with g++-12 12.4.0 and clang++-18 18.1.3), OpenSSL, zlib, SQLite 3,
libcurl, Boost (headers), nlohmann/json and GoogleTest (tests only). On Debian or Ubuntu:

```sh
sudo apt-get install -y build-essential cmake libssl-dev zlib1g-dev libsqlite3-dev \
    libcurl4-openssl-dev libboost-dev nlohmann-json3-dev libgtest-dev
```

Clang and GCC builds in separate directories (use `-j1` with Clang on an 8 GiB machine):

```sh
CXX=g++-12 CC=gcc-12 cmake -S . -B build/gcc -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON
CXX=clang++-18 CC=clang-18 cmake -S . -B build/clang -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON
cmake --build build/clang -j1 && ctest --test-dir build/clang --output-on-failure
```

Testing and checks:

| Command | What it runs |
|---|---|
| `ctest --test-dir build/Release --output-on-failure` | All offline unit, protocol, fake-server and lifecycle tests |
| `bash tools/run_sanitizers.sh` | Same tests under AddressSanitizer and UndefinedBehaviorSanitizer (`build-san/`) |
| `bash tools/run_tsan.sh` | Concurrency tests under ThreadSanitizer (`build-tsan/`, slow with `-j1`) |
| `bash tools/run_fuzz_long.sh 600` | Four libFuzzer targets, clang-18, 600 s each (`build-fuzz/`); see `fuzz/README.md` |
| `bash tools/check_headers.sh` | Every public header compiles on its own |
| `bash tools/check_examples.sh` | Examples compile |

Nothing in these checks contacts Telegram. Live testing is a separate step: see [guides/PRODUCTION_READINESS.md](guides/PRODUCTION_READINESS.md).

CMake options: `ENABLE_TESTS`, `BUILD_LIVE_CHECK`, `BUILD_LIVE_TESTS`, `BUILD_SHARED_LIBS`, `ENABLE_SANITIZERS`,
`ENABLE_COVERAGE`, `BUILD_DOCUMENTATION`.

Build speed options (all optional, all fall back to the plain toolchain):

| Option | Default | Effect |
|---|---|---|
| `CPPGRAM_USE_CCACHE` | `ON` | Allow a compiler cache at all |
| `CPPGRAM_COMPILER_CACHE` | `auto` | `auto`, `ccache`, `sccache` or `off`. A named tool that is missing is an error |
| `CPPGRAM_FAST_LINKER` | `auto` | `auto` (mold, then lld), `mold`, `lld` or `off`. Each candidate is test-linked first |
| `CPPGRAM_UNITY_BUILD` | `OFF` | Merge sources into batches of 8 per target. Faster clean builds, slower single-file rebuilds |

`CMAKE_EXPORT_COMPILE_COMMANDS` is always on, so `build/<type>/compile_commands.json` exists for clangd and clang-tidy.

## Usage in CMake

After installing, link the library target:

```cmake
find_package(cppgram CONFIG REQUIRED)
target_link_libraries(your_bot PRIVATE cppgram::cppgram)
```

Or add the repository directly with `add_subdirectory` (for example as a Git submodule).

## Live check

`cppgram_live_check` runs a real connection against Telegram: TCP connect, auth key handshake, `initConnection`,
`login_bot` (when a token is given) and `get_me`. Credentials come only from environment variables:

```sh
cd build/Release
TG_API_ID=... TG_API_HASH=... TG_BOT_TOKEN=... ./cppgram_live_check
```

Full details: [LIVE_CHECK.md](LIVE_CHECK.md).

## Layout

- `include/cppgram/client.h`: MTProto client API
- `include/cppgram/types.h`: typed wrappers (User, Chat, Message, Poll, Sticker, ...)
- `include/cppgram/filters.h`, `parser.h`, `storage.h`, `session_string.h`: filters, formatted text, storage, session strings
- `include/cppgram/mtproto/`: crypto, TL, transports, auth, connection, session, SRP
- `include/cppgram/mtproto/schema/api.h` and `src/mtproto/schema/api_impl_*.cpp`: generated schema code
- `include/cppgram/bot/`, `include/cppgram/bot.h`: Bot API layer (`cppgram::bot`)
- `src/`: client implementation, split by area (`client_*.cpp`)
- `tests/`: `mtproto/` suites for the client, `bot/` suites for the Bot API layer
- `examples/`: Bot API examples carried over from tgbot-cpp (not yet ported to the MTProto client)

## Updating the Telegram schema

See [HOW_TO_UPDATE_TELEGRAM_API.md](HOW_TO_UPDATE_TELEGRAM_API.md).

## Licence and credits

cppgram is released under the MIT License for the original tgbot-cpp parts (see [LICENSE](LICENSE)). The MTProto
layer derives its schema from Kurigram (LGPL-3.0). See [NOTICE](NOTICE) for the full attribution.

The Bot API layer is derived from [tgbot-cpp](https://github.com/reo7sp/tgbot-cpp) by Oleg Morozenkov and contributors.

## Live Telegram tests

`cppgram_live_tests` runs six tests against real Telegram or your proxy: reconnect, flood_wait, socks5,
session_restore, mtproto_proxy and padded_framing. They need credentials in the environment and report
PASS, FAIL, SKIP or NOT_SUPPORTED. See `guides/LIVE_TESTS.md`. Offline checks for the runner:
`tools/check_live_tests_offline.sh <binary>`.
