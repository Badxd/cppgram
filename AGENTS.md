# Working on cppgram

Guidance for people and coding agents working in this repository.

- Use the `make` targets. They are listed in `Makefile` and in `README.md`.
- Build and test: `make test`. It configures a Release build with tests, builds, and runs `ctest`.
- After changing `schema/*.tl` or the generator, run `make tl-generate` and `make schema-check`.
- After changing `VERSION`, `app_version` in `include/cppgram/client.h`, or the version in the changelog, run
  `bash tools/check_version.sh`.
- After changing `CMakeLists.txt` source lists, run `python3 tools/check_cmake_sources.py`.
- Formatting: `make format-check` checks the files in `tools/format_allowlist.txt` only. Do not reformat unrelated files.
  Do not run `clang-format -i` on generated files under `src/mtproto/schema/` or `include/cppgram/mtproto/schema/`.
- Generated files, do not hand-edit: `include/cppgram/mtproto/schema/api.h`, `src/mtproto/schema/api_impl_*.cpp`.
  `src/ApiMethods.cpp` and `include/cppgram/bot/ApiMethods.inc.h` are also generated; their generator is not in this
  repository (see `HOW_TO_UPDATE_TELEGRAM_API.md`).
- Do not add live Telegram calls to ordinary tests. Live code runs only through the manual `live` workflow and the
  binaries in `examples/live_tests` and `examples/live_check.cpp`, and those need owner credentials.
- Never put credentials, session files, phone numbers or OTPs in the repository, in logs, or in test fixtures. Test
  fixtures use labelled placeholders.
- Do not claim a result that was not run. Mark it NOT RUN, and say which environment it was or was not run in.
- Versioning and release steps: `HOW_TO_RELEASE.md`.
