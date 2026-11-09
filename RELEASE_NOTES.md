# cppgram 0.1.0 - release notes (pre-release candidate)

**Status: private pre-release candidate. Not production-ready. Not approved for public release.**

cppgram is a C++20 library with two parts:

- `cppgram::Client`: an MTProto client that logs in as a bot or a user and talks to Telegram directly.
- `cppgram::bot`: an HTTP Bot API layer. Its generated files cannot be regenerated from this repository (see
  `HOW_TO_UPDATE_TELEGRAM_API.md`).

## Why 0.1.0

No version has been tagged or published. The client already announces `0.1.0` to Telegram as its `app_version`.
The live Telegram path has not been run, so the release is pre-1.0 and the API may change in a minor version.
See `CHANGELOG.md` for the content and `VERSION` for the single source of the number.

## What was verified, and by what

| Claim | Evidence | Scope |
|---|---|---|
| 624 offline test cases pass | `TEST_RESULTS.md`, historical run | GCC 13.3, Release, x86_64 Ubuntu 24.04, fake DCs and loopback sockets. Run before this release-preparation pass. Not rerun. |
| Generated TL code is reproducible | `tools/check_schema.sh`, `schema/SHA256SUMS.txt` | Run in the release-preparation pass |
| CMake source lists build every file | `tools/check_cmake_sources.py` | Run in the release-preparation pass |
| Examples compile | `tools/check_examples.sh` | Syntax only, GCC 13.3. Nothing is linked or run. |

## What was not verified

- Live Telegram behaviour. Every live check is NOT RUN. The owner runs them on a VPS (`VPS_TESTING_GUIDE.md`).
- ARM64 (AArch64). No ARM64 build was run.
- Sanitizers. ASan, UBSan and TSan are set up in CI and as configure targets, but were not run here.
- A 1 GB machine. The largest single compile needs about 2.8 GB.
- Behaviour against Kurigram. The parity matrix is a source and evidence classification, not a behavioural audit.

## Compatibility

- Requires a C++20 compiler (GCC 13 verified; Clang 18 syntax-checked only), CMake 3.16 or newer, OpenSSL, zlib, SQLite 3,
  libcurl, Boost headers, nlohmann/json and GoogleTest for the tests.
- Linux only. Windows is not supported or tested.
- Downstream: `find_package(cppgram CONFIG REQUIRED)`. The package version file uses `SameMajorVersion`.

## Licensing

Read `NOTICE` and `LICENSE` before any public release. The code is derived from tgbot-cpp (MIT). The schema provenance is
stated inconsistently in the repository (see `schema/README.md`). This is a review item for the owner, not resolved here.

## Known limitations

See `KNOWN_LIMITATIONS.md`. The most important: three methods are implemented with limits (`ephemeral.SendMessage`,
`ephemeral.EditMessage`, `messages.GetInlineBotResults`); one method is blocked by the schema
(`channels.UpdatePinnedForumTopic`); secret chats and calls are out of scope.
