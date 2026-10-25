# cppgram

> C++ Telegram library by [Badmunda05](https://github.com/Badmunda05/cppgram). Group: https://t.me/PBXCHATS · Updates: https://t.me/PBX_UPDATE · Owner: https://t.me/BadMundaXD

## Status

| Part | State |
|---|---|
| Python removed from the build and tooling | done |
| Crypto: SHA1/SHA256, CRC32, AES-IGE, MTProto 2.0 packets, RSA, DH, pq factorization | done, tested |
| TL serialization (int, long, double, string, bytes, Bool, int128/256, Vector, bare vector, Object) | done, tested |
| Transport codecs: abridged, intermediate, full | done, tested |
| Auth key handshake (req_pq_multi, req_DH_params, set_client_DH_params) | done, tested against an in-process fake server |
| TCP connection (POSIX sockets, intermediate transport) | done, tested on loopback |
| Encrypted session: rpc_result, rpc_error, bad_server_salt retry, msg_container, gzip_packed | done, tested against a fake server |
| Session file storage | done, tested |
| TL codegen in C++ (`cppgram_tl_codegen`) from Kurigram layer 232 schema: 1754 types, 869 functions | done |
| High-level client: connect, login (bot, 2FA SRP), messages, chats, users, bots, media, keyboards, polls, stickers, forum topics, folders, invite links, join requests, privacy, sessions | done, offline tested (141 tests), not live-tested |
| Updates: pts/qts/seq gap detection, getDifference, SQLite state, handler groups, error hook | done, offline tested, not live-tested |
| Parser: HTML and MarkdownV2 into entities with UTF-16 offsets | done, tested |
| CDN downloads with AES-256-CTR and per-part SHA-256 check | done, not live-tested |
| Parked (outside the core scope): stories, payments and stars, business, gifts, boosts, wallet | not started |

Live testing needs a Telegram API id/hash and access to a DC. Everything marked "not live-tested" above is checked only
against offline tests and our own fake server. Run `cppgram_live_check` (see LIVE_CHECK.md) to verify against Telegram.

## Build

```sh
make build        # cmake configure + build (library, tests, codegen tool)
make test         # runs gtest suites (bot API suite and cppgram MTProto suite)
make tl-generate  # regenerate schema code from schema/*.tl
```

Dependencies: CMake 3.16+, C++20 compiler, OpenSSL, zlib, Boost (headers), libcurl, nlohmann_json, GoogleTest.

## Layout

- `include/cppgram/mtproto/` crypto, tl, transport, auth, connection, session headers
- `include/cppgram/mtproto/schema/api.h` and `src/mtproto/schema/api_impl_*.cpp` generated TL types and methods
- `schema/*.tl` Telegram schema (from Kurigram, LGPL-3.0)
- `tools/tl_codegen/` C++ schema generator
- `include/cppgram/client.h` high-level client
- `include/cppgram/bot/` Bot API layer (derived from tgbot-cpp, MIT)

## Example

```cpp
#include "cppgram/client.h"

cppgram::ClientOptions opt;
opt.api_id = 12345;
opt.api_hash = "your_api_hash";
cppgram::Client client(opt);
client.connect();
client.login_bot("123456:BOT_TOKEN");
```

## License

MIT for the original tgbot-cpp parts (see LICENSE). The MTProto layer derives its schema from Kurigram (LGPL-3.0); see NOTICE.
