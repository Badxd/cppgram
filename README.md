# cppgram

C++20 library for Telegram. It has two parts:

- **MTProto client** (`cppgram::Client`): signs in as a bot or a user and talks to Telegram directly.
- **Bot API layer** (`cppgram::bot`): an HTTP Bot API wrapper, derived from [tgbot-cpp](https://github.com/reo7sp/tgbot-cpp).

Repository: [Badmunda05/cppgram](https://github.com/Badmunda05/cppgram)

## Status

Version 0.1.0, a pre-release candidate. It is not production-ready and not approved for public release.

- **Offline tests:** 624 test cases in 64 files, all passing in one historical run on GCC 13.3 Release, against fake
  data centres and local sockets. Per-binary counts are in `TEST_RESULTS.md`.
- **Live Telegram:** NOT RUN. Nothing in this release has been run against Telegram. The owner runs the live and VPS
  steps in `VPS_TESTING_GUIDE.md`.
- **Platforms:** Linux x86_64 with GCC 13.3 is the only platform built and tested. ARM64 and Clang are prepared in CI and
  not run. Windows is not supported. See `docs/PORTABILITY.md`.
- **Kurigram parity:** source and evidence classification only, see `PARITY_MATRIX.md`. No behavioural parity is claimed.
- **Limits:** see `KNOWN_LIMITATIONS.md`. Fake-TLS MTProto proxy secrets are rejected. Secret chats and calls are not
  implemented.

For the reasoning behind the version number, see `RELEASE_NOTES.md`.

## Example

An echo bot with the MTProto client. This is a compile-checked snippet, not a run result.

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

The full API is described in [`docs/API.md`](docs/API.md). Every `Client` member is listed in
[`docs/API_INDEX.md`](docs/API_INDEX.md). More programs are in [`examples`](examples). They are compile-checked, not run.

## Build

Requirements: CMake 3.16 or later, a C++20 compiler (GCC 13.3 was used for verification; a Clang 18 job is defined in CI but has not run), OpenSSL, zlib, SQLite 3, libcurl, Boost (headers), nlohmann/json, and GoogleTest for the tests. On Ubuntu 24.04:

```sh
sudo apt-get install -y build-essential cmake ninja-build libssl-dev zlib1g-dev libsqlite3-dev \
    libcurl4-openssl-dev libboost-dev nlohmann-json3-dev libgtest-dev
```

Common make targets:

| Target | What it does |
|---|---|
| `make build` | Configure and build the library and the tests (Ninja when installed) |
| `make test` | Run the offline gtest suites. Contacts no network |
| `make install INSTALL_PREFIX=build/install` | Install headers, library and CMake package files |
| `make checks` | Source-list, version, format-allowlist and example checks |
| `make schema-check` | Regenerate the TL code and confirm it matches the checked-in files |
| `make package` | Build the source release zip into `dist/` |
| `make verify-release` | Check a release zip against its bundled checksums |
| `make live` | Build the live-test binaries in `build/live`. Does not run them |
| `make live-offline-check` | Check live-test wiring with no network and no credentials |

Build options:

| Option | Default | Effect |
|---|---|---|
| `ENABLE_TESTS` | `OFF` | Build the test suites |
| `BUILD_LIVE_CHECK` / `BUILD_LIVE_TESTS` | `OFF` | Build the live binaries. They are not run by the build |
| `BUILD_SHARED_LIBS` | `OFF` | Build a shared library |
| `ENABLE_SANITIZERS` / `ENABLE_COVERAGE` | `OFF` | ASan and UBSan, or gcov coverage |
| `CPPGRAM_USE_CCACHE` | `ON` | Use a compiler cache when one is installed |
| `CPPGRAM_FAST_LINKER` | `auto` | `auto` (mold, then lld), `mold`, `lld` or `off` |
| `CPPGRAM_UNITY_BUILD` | `OFF` | Merge sources into batches. Faster clean builds |

The largest generated file needs about 2.8 GB to compile. Use `-j1` on a machine with less than 8 GB of RAM. See
`BUILD_RESULTS.md`.

## Errors, logging and Web Apps

- `cppgram/errors.h`: typed errors in the `cppgram::errors` namespace, derived from `errors::Error`.
- `cppgram/log.h`: structured logging with levels, components, request ids and secret redaction. The default level is WARN.
- `cppgram/webapp.h`: verifies Web App `initData`. It returns parsed data only when the signature and time checks pass.
  This is not a cryptographic audit.

## Usage from CMake

```cmake
find_package(cppgram 0.1 CONFIG REQUIRED)
target_link_libraries(your_bot PRIVATE cppgram::cppgram)
```

Or add the repository with `add_subdirectory`.

## Live tests

`cppgram_live_check` runs a real connection: TCP, the auth key handshake, `initConnection`, `login_bot` and `get_me`.
`cppgram_live_tests` runs six tests: reconnect, flood_wait, socks5, session_restore, mtproto_proxy and padded_framing.
Credentials come only from environment variables. The live workflow is manual and secret-gated; it is never run
automatically. See [`guides/LIVE_TESTS.md`](guides/LIVE_TESTS.md) and [`VPS_TESTING_GUIDE.md`](VPS_TESTING_GUIDE.md).

## Layout

- `include/cppgram/client.h`: MTProto client API
- `include/cppgram/types.h`: typed wrappers (User, Chat, Message, Poll, Sticker, ...)
- `include/cppgram/bot.h`, `include/cppgram/bot/`: Bot API layer
- `include/cppgram/mtproto/`: crypto, TL, transports, auth, connection, session
- `src/`: client implementation, one file per area
- `src/mtproto/schema/`: generated schema code (19 `api_impl` files), reproducible with `make schema-check`
- `schema/`: TL input files and `README.md` on provenance
- `tests/`: `mtproto/` suites for the client, `bot/` suites for the Bot API layer
- `examples/`: example programs
- `tools/`: checks, schema generator, packaging and verification scripts

## Documentation

- [`FINAL_REPORT.md`](FINAL_REPORT.md): the release report
- [`IMPLEMENTATION_STATUS.md`](IMPLEMENTATION_STATUS.md): status by area
- [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md), [`PARITY_MATRIX.md`](PARITY_MATRIX.md),
  [`TEST_RESULTS.md`](TEST_RESULTS.md), [`BUILD_RESULTS.md`](BUILD_RESULTS.md)
- [`docs/API.md`](docs/API.md) and [`docs/API_INDEX.md`](docs/API_INDEX.md): the public API
- [`docs/PORTABILITY.md`](docs/PORTABILITY.md): platform notes
- [`VPS_TESTING_GUIDE.md`](VPS_TESTING_GUIDE.md): the owner's VPS and live test plan
- [`CHANGELOG.md`](CHANGELOG.md), [`RELEASE_NOTES.md`](RELEASE_NOTES.md), [`HOW_TO_RELEASE.md`](HOW_TO_RELEASE.md)
- [`HOW_TO_UPDATE_TELEGRAM_API.md`](HOW_TO_UPDATE_TELEGRAM_API.md): updating the schema
- [`guides/`](guides/README.md): installation, authentication, sessions, architecture, proxies, threat model
- [`docs/historical/`](docs/historical/): superseded reports, kept for the record. Each has a HISTORICAL banner

## Licence

MIT for the tgbot-cpp-derived parts, see [LICENSE](LICENSE). The MTProto schema header cites Kurigram (LGPL-3.0), and
the schema sources conflict on provenance. See [NOTICE](NOTICE) and `schema/README.md`. Licence review is an open item
for the owner.
