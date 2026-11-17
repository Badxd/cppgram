# CppGram

CppGram is a C++20 library for Telegram. It has two parts:

- **MTProto client** (`cppgram::Client`): signs in as a bot or a user and talks to Telegram servers directly.
- **Bot API layer** (`cppgram::bot`): an HTTP Bot API wrapper. It is derived from
  [tgbot-cpp](https://github.com/reo7sp/tgbot-cpp) (MIT); see [NOTICE](NOTICE).

Version **1.0.0 is a release candidate. It is not published and not declared stable.** The release decision and its
blockers are in [`FINAL_V1.0.0_READINESS_REPORT.md`](FINAL_V1.0.0_READINESS_REPORT.md).

Repository: [Badmunda05/cppgram](https://github.com/Badmunda05/cppgram)

## Features

- MTProto client with bot login and user login, a typed API (`include/cppgram/client.h`), and update handling with
  filters and handler groups.
- Typed wrappers for users, chats, messages, polls, stickers and more (`include/cppgram/types.h`).
- Session files in a versioned format (`CGS2`, with a CRC-32 check). Update state is kept in a SQLite store (`SqliteStorage`).
- MTProxy support for plain and `dd`-prefixed secrets. Fake-TLS (`ee`) secrets are refused by design.
- Bot API layer for HTTP-based bots (`include/cppgram/bot.h`).
- Web App `initData` verification (`cppgram/webapp.h`); see "Limits" below.
- Structured logging with secret redaction (`cppgram/log.h`) and typed errors (`cppgram/errors.h`).

## Platforms and compilers

| Platform | Compiler | Status |
|---|---|---|
| Linux x86_64 (Ubuntu 24.04 sandbox) | GCC 13.3.0 | Built and tested |
| Linux x86_64 (Ubuntu 24.04 sandbox) | Clang 18.1.3 | Built and tested |
| Linux ARM64 | any | NOT RUN |
| Windows | any | Unsupported |
| macOS | any | Not tested |

Requires a C++20 compiler and CMake 3.16 or later. See [`docs/PORTABILITY.md`](docs/PORTABILITY.md).

## Dependencies

- OpenSSL, zlib, SQLite 3, libcurl
- Boost (headers only)
- nlohmann/json
- GoogleTest, only when building the tests

On Ubuntu 24.04:

```sh
sudo apt-get install -y build-essential cmake ninja-build libssl-dev zlib1g-dev libsqlite3-dev \
    libcurl4-openssl-dev libboost-dev nlohmann-json3-dev libgtest-dev
```

## Installation

Build and install to a prefix:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/install
cmake --build build
cmake --install build
```

Or with the Makefile wrapper: `make build` and `make install INSTALL_PREFIX=build/install`. The full target list is
under "Build and test".

## CMake integration

After installation, in your `CMakeLists.txt`:

```cmake
find_package(CppGram 1.0.0 CONFIG REQUIRED)
target_link_libraries(your_bot PRIVATE cppgram::cppgram)
```

Or add the repository as a subdirectory and link the same target. The independent consumer in
[`verify/consumer`](verify/consumer) is a complete, minimal example of this.

## Minimal example

This is an echo bot with the MTProto client. The snippet was compile-checked; it has not been run against Telegram.

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

The full API is in [`docs/API.md`](docs/API.md), and every `Client` member is listed in
[`docs/API_INDEX.md`](docs/API_INDEX.md). Programs under [`examples`](examples) are syntax-checked, not run.

## Authentication and sessions

- `login_bot(token)` signs in with a bot token. User login requests a code and then calls `sign_in_code()`, with
  `sign_in_password()` when two-factor authentication is enabled. See `guides/`.
- `ClientOptions::session_path` names the session file. Sessions are written in the `CGS2` format with a CRC-32
  check. A corrupt file raises `SessionCorruptError`.
- Session strings and auth keys are secrets. Do not commit them or paste them into issues.
- Guides: [`guides/AUTHENTICATION.md`](guides/AUTHENTICATION.md), [`guides/SESSIONS.md`](guides/SESSIONS.md).

## MTProto

- The TL schema is generated into `src/mtproto/schema/` from the files in `schema/`. `make schema-check` regenerates
  it and confirms it matches the checked-in code. Provenance is in [`schema/README.md`](schema/README.md).
- Transports: intermediate, abridged and full, with obfuscated framing. Padded intermediate is used for `dd` proxy
  secrets.
- DC migration (`USER_MIGRATE_N`, `PHONE_MIGRATE_N`) is handled; the DC number must be in 1 to 99.
- `FLOOD_WAIT` sleeps can be cancelled.

## Proxy support

- Plain MTProxy secrets and `dd`-prefixed secrets are supported. The `dd` secret is `dd` followed by 32 hex characters.
- Fake-TLS (`ee`) secrets are refused: `ProxySecretKind::FakeTls` is rejected by design.
- Framing decisions are in [`PROXY_AUDIT.md`](PROXY_AUDIT.md).

## Error handling and logging

- Failures are reported as typed errors from `cppgram/errors.h`, derived from `errors::Error`.
- `cppgram/log.h` provides levels, components, request ids and redaction of secrets. The default level is WARN.

## Web App initData

`cppgram/webapp.h` verifies `initData` and returns the parsed data only when the signature and time checks pass. The
checks are covered by `tests/security/webapp_test.cpp`. This is not a cryptographic audit.

## Build and test

Requirements are listed under "Dependencies". Common Makefile targets:

| Target | What it does |
|---|---|
| `make build` | Configure and build the library and the tests |
| `make test` | Run the offline test suites. Contacts no network |
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
| `BUILD_LIVE_CHECK` / `BUILD_LIVE_TESTS` | `OFF` | Build the live binaries. The build does not run them |
| `BUILD_SHARED_LIBS` | `OFF` | Build a shared library |
| `ENABLE_SANITIZERS` / `ENABLE_COVERAGE` | `OFF` | ASan and UBSan, or gcov coverage |
| `CPPGRAM_USE_CCACHE` | `ON` | Use a compiler cache when one is installed |
| `CPPGRAM_FAST_LINKER` | `auto` | `auto` (mold, then lld), `mold`, `lld` or `off` |
| `CPPGRAM_UNITY_BUILD` | `OFF` | Merge sources into batches. Faster clean builds |

The largest generated file needs about 2.8 GB to compile. Use `-j1` on a machine with less than 8 GB of RAM. See
[`BUILD_RESULTS.md`](BUILD_RESULTS.md).

Verified results, with exact commands, exit codes and counts, are in [`TEST_RESULTS.md`](TEST_RESULTS.md):

- CTest: 711 of 711 cases pass with GCC 13.3.0 and with Clang 18.1.3 (Release, C++20).
- ASan and UBSan on the full suite, no reports (Clang 18.1.3).
- TSan on all six test binaries (711 tests), Clang 18.1.3: no reports.
- ASan and UBSan on the full suite (711 of 711), Clang 18.1.3: no reports.
- Seven libFuzzer targets, 120 s each on an instrumented library: no findings. Bounds and limits are in [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md).

## Live testing

Live checks contact real Telegram servers or a proxy you configure. They need your own credentials or bot token, and
in some cases an unrestricted network. No live result has been produced for this release. Every live check so far is
BLOCKED.

- **Direct live checks** in this repository: `cppgram_live_check` and `cppgram_live_tests` (`make live`).
- **CppGramLiveTest**: separate live-testing project with a categorised runner and a bot-controlled runner. It is
  delivered as its own archive, is not part of this repository, and is hosted at
  [github.com/Badxd/Cpplivetest](https://github.com/Badxd/Cpplivetest).

Credentials come only from environment variables. Read [`LIVE_TESTING.md`](LIVE_TESTING.md) for the variable names and
how to read results. Do not put API hashes, bot tokens, session strings or proxy secrets in an issue.

## Examples

Programs under [`examples/`](examples) are syntax-checked against the public headers. They are not run in the release
checks. Upstream tgbot-cpp material that is not a CppGram example is kept separately under
[`reference/tgbot-cpp-upstream/`](reference/tgbot-cpp-upstream/README.md).

## Limits

- Live Telegram validation: not run for this release (see "Live testing").
- ARM64: not run. Windows: unsupported. macOS: not tested.
- Fake-TLS (`ee`) MTProxy secrets: refused by design. Secret chats and calls are not implemented.
- TDLib parity for plain trailing-byte handling: BLOCKED. The upstream source was not reachable.
- Behavioural parity with other libraries is not claimed. See [`PARITY_MATRIX.md`](PARITY_MATRIX.md).

The full list is in [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md).

## Security

Report security issues privately through the contact details below, not in a public issue. The security review, its
open items and the evidence tests are in [`security-review.md`](security-review.md). Also see [`SECURITY.md`](SECURITY.md).

## Contributing

Open an issue or a pull request. Run `make checks` and `make test` before sending a change. Do not include credentials,
session files or proxy secrets in a report or a test. Changes to the TL schema must follow
[`HOW_TO_UPDATE_TELEGRAM_API.md`](HOW_TO_UPDATE_TELEGRAM_API.md).

## Documentation

- [`FINAL_REPORT.md`](FINAL_REPORT.md): release verification report
- [`FINAL_V1.0.0_READINESS_REPORT.md`](FINAL_V1.0.0_READINESS_REPORT.md): release decision and blockers
- [`docs/API.md`](docs/API.md), [`docs/API_INDEX.md`](docs/API_INDEX.md): public API
- [`docs/PORTABILITY.md`](docs/PORTABILITY.md): platform notes
- [`guides/`](guides/README.md): installation, authentication, sessions, architecture, proxies, threat model
- [`CHANGELOG.md`](CHANGELOG.md), [`RELEASE_NOTES.md`](RELEASE_NOTES.md), [`HOW_TO_RELEASE.md`](HOW_TO_RELEASE.md)
- [`HOW_TO_UPDATE_TELEGRAM_API.md`](HOW_TO_UPDATE_TELEGRAM_API.md): updating the schema
- [`docs/historical/`](docs/historical/): superseded reports, kept for the record. Each carries a HISTORICAL banner

## Layout

- `include/cppgram/`: public headers (`client.h`, `types.h`, `bot.h`, `mtproto/`, ...)
- `src/`: implementation
- `src/mtproto/schema/`: generated TL code, reproducible with `make schema-check`
- `schema/`: TL input files and provenance
- `tests/`: offline test suites
- `examples/`: example programs (syntax-checked)
- `verify/consumer/`: independent consumer project for the installed package
- `reference/tgbot-cpp-upstream/`: third-party upstream material, not CppGram examples
- `tools/`: checks, schema generator, packaging and verification scripts

## Contact

- Email: mumda.bad1322@gmail.com
- Telegram group: https://t.me/PBXCHATS
- Updates channel: https://t.me/PBX_UPDATE

## Licence

MIT. See [LICENSE](LICENSE). The licence file carries the tgbot-cpp copyright for the derived Bot API layer. The MTProto
schema header cites Kurigram (LGPL-3.0), and the schema sources conflict on provenance. See [NOTICE](NOTICE) and
[`schema/README.md`](schema/README.md). Licence review of the schema provenance is an open item for the owner.
