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

## Build

```sh
make build        # configure and build the library, tests and live check
make test         # run the gtest suites
make tl-generate  # regenerate the TL code from schema/*.tl (C++ tool, no Python)
make install INSTALL_PREFIX=build/install
```

Dependencies: CMake 3.16+, a C++20 compiler, OpenSSL, zlib, SQLite 3, libcurl, Boost (headers), nlohmann/json and
GoogleTest (tests only). On Debian or Ubuntu:

```sh
sudo apt-get install -y build-essential cmake libssl-dev zlib1g-dev libsqlite3-dev \
    libcurl4-openssl-dev libboost-dev nlohmann-json3-dev libgtest-dev
```

CMake options: `ENABLE_TESTS`, `BUILD_LIVE_CHECK`, `BUILD_SHARED_LIBS`, `ENABLE_SANITIZERS`, `ENABLE_COVERAGE`,
`BUILD_DOCUMENTATION`.

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
