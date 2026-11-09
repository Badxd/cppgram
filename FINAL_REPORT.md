# CppGram pre-VPS release report (private)

**Status in one line:** pre-release candidate 0.1.0. The final source pass ran the 624 offline test cases on GCC 13.3
Release (historical evidence, not rerun here). This pre-VPS pass prepared the repository, the build and CI files, the
documentation and the release package, and ran targeted checks listed below. Nothing was run against Telegram, no VPS was
used, and no GitHub workflow was executed. **Live Telegram: NOT RUN. VPS: NOT RUN. OWNER WILL TEST.**

This report does not claim behavioural parity with Kurigram, production readiness, live behaviour, ARM64 support or
that the release fits in 1 GB. Each is NOT RUN or not evidenced. Limits are in `KNOWN_LIMITATIONS.md`. Status by area is in
`IMPLEMENTATION_STATUS.md`.

The SHA-256 and file count of the release zip are not written inside it, because a file cannot record its own hash. They
are reported in the delivery message for this pass.

## Headline status

| Item | Result | Where |
|---|---|---|
| Offline test cases (historical, final source pass) | 624 run, 624 passed, 0 failed, 0 disabled, 64 files, six binaries | `TEST_RESULTS.md`, `BUILD_RESULTS.md` 4c. Not rerun in this pass |
| Kurigram reference entries | 330 namespaced methods: 326 IMPLEMENTED (153 with a named passing offline test, 173 source only), 3 IMPLEMENTED_WITH_LIMITATIONS, 1 SCHEMA_BLOCKED | `PARITY_MATRIX.md` |
| Largest single compile | 2,836,644 KiB (`api_impl_17.cpp`), historical measurement | `BUILD_RESULTS.md` 3b |
| x86_64 (Linux, GCC 13.3) | Built and tested (historical). Pre-VPS configure, install, consumer and `make checks` ran in this pass | `BUILD_RESULTS.md`, section below |
| ARM64 | NOT RUN. The CI job is gated by `ENABLE_ARM64_CI` and is off by default | `docs/PORTABILITY.md` |
| Sanitizers (ASan, UBSan, TSan) | NOT RUN in this pass | `BUILD_RESULTS.md` 4h |
| GitHub workflows | NONE EXECUTED. Written, YAML-checked, least-privilege | section below |
| Live Telegram | NOT RUN | `VPS_TESTING_GUIDE.md` steps O to Q |
| VPS (1 GB) | NOT RUN, NOT MEASURED. The largest compile needs about 2.8 GB | `BUILD_RESULTS.md` 6 |

## Checks run in this pass

| Check | Command or method | Result |
|---|---|---|
| Full local check gate | `make checks` (source list, version, format allowlist, API index, example syntax) | exit 0 |
| Examples | `tools/check_examples.sh` (17 files, `-fsyntax-only`) | 17 OK, 0 FAIL |
| API index | `python3 tools/api_index.py --check` | 470 distinct `Client` members, all defined |
| Schema reproducibility | `make schema-check` (generator built from repository source, then `tools/check_schema.sh`) | exit 0. `api.h` and 19 `api_impl` files OK, layer 233 agrees, file set matches, `schema/SHA256SUMS.txt` 24 entries OK |
| Fresh configure | `cmake -S . -B <scratch>/build-fresh -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON` | exit 0, dependencies found, `cppgramConfigVersion.cmake` reports 0.1.0 |
| Reconfigure of the existing build | `cmake .` in the release build tree | exit 0 |
| Install | `cmake --install <build> --prefix <scratch>/prefix` | exit 0. 55 headers, 3 CMake files, `libcppgram.a` |
| Consumer project | a small project that calls `find_package(cppgram 0.1 CONFIG REQUIRED)` and links `cppgram::cppgram` | configure, build, run: exit 0, no unresolved shared libraries |
| Workflow YAML | `yaml.safe_load` on all three workflow files | parse OK |
| Shell scripts | `bash -n` on every `tools/*.sh` used by CI or the release | OK |
| Python tools | `py_compile` on the packaging and check scripts | OK |
| Markdown links | relative links in every top-level, `docs/`, `guides/` and `schema/` Markdown file | all resolve, after fixes to two historical links |

`actionlint` and `shellcheck` were not available, so the workflows and scripts were not linted by those tools.

The 624-case test suite, the sanitizer builds, Valgrind and every live step were not run in this pass. The historical
results are labelled as such wherever they appear.

## Repository changes in this pass

**Removed.** `Dockerfile`, `.dockerignore`, `heroku.yml`, `app.json` (Heroku and Docker deploy files that referenced an
external URL), `env.example` (merged into `.env.example`), and the stale root `SHA256SUMS.txt`. `tools/__pycache__` was
removed and is excluded from the release.

**Replaced or rewritten.** `AGENTS.md`, `HOW_TO_UPDATE_TELEGRAM_API.md`, `HOW_TO_RELEASE.md`, `README.md`,
`.github/workflows/ci.yml`, `CHANGELOG.md` (new `0.1.0` section), `IMPLEMENTATION_STATUS.md`, `KNOWN_LIMITATIONS.md`
(items 48 to 61 added), `IMPLEMENTATION_STATUS.md`, `FINAL_REPORT.md`, `VPS_TESTING_GUIDE.md` (section added; the
owner's steps are unchanged). `docs/LIVE_TESTS.md`, `guides/BENCHMARKS.md` and `docs/KURIGRAM_PARITY.md` are pointer stubs.

**Added.**
- `RELEASE_NOTES.md`: status, basis for 0.1.0, what was and was not verified.
- `docs/API.md` and `docs/API_INDEX.md`: the public API guide and the generated `Client` member index.
- `docs/PORTABILITY.md`: platform status, portability points from the source, ARM64 and cross-build notes (unverified).
- `schema/README.md` and `schema/SHA256SUMS.txt`: schema inputs, provenance conflict, generator, checksums.
- `tools/api_index.py`, `tools/check_cmake_sources.py`, `tools/check_version.sh`, `tools/format_check.sh` with
  `tools/format_allowlist.txt`, `tools/update_schema_checksums.sh`, `tools/package_release.py`, `tools/verify_release.sh`.
- `.github/workflows/release.yml` (manual only) and `.github/workflows/live.yml` (manual only).

**Changed.** `VERSION` is `0.1.0`. `CMakeLists.txt` reads the version from `VERSION`, writes a package version file with
`SameMajorVersion`, and lists `api_impl_0` to `api_impl_18`. `cmake/cppgramConfig.cmake.in` now finds OpenSSL, ZLIB and
SQLite3 as well as Boost, CURL, nlohmann_json and Threads, so a static consumer links without extra flags. `Makefile`
gained `checks`, `source-check`, `version-check`, `format-check`, `format`, `check-examples`, `api-check`, `schema-check`,
`configure-asan`, `configure-tsan`, `package` and `verify-release`. `.gitignore` excludes `dist/`. `.env.example` lists
placeholders only. `guides/LIVE_TESTS.md` had its Heroku section removed.

## Build system

- Package version comes from `VERSION`. `client.h` `app_version` is `0.1.0`. `tools/check_version.sh` checks that they match
  and that the changelog has a `0.1.0` heading.
- Install and export: `cppgramTargets`, `cppgramConfig.cmake`, `cppgramConfigVersion.cmake`. A consumer project verified
  in this pass uses `find_package(cppgram 0.1 CONFIG REQUIRED)`.
- Library and test source lists are checked against the files on disk by `tools/check_cmake_sources.py`: 112 library
  sources and 64 test sources.
- Configure, build, test, install, format-check, schema-check, clean and sanitizer targets are in the Makefile, and CI uses
  the same flags.

## Schema

- Layer 233 is unchanged. `api.h` is unchanged. The generated implementation files are split into 19 `api_impl_N.cpp`
  files of 175 function bodies each (historical change, `BUILD_RESULTS.md` section 3).
- `schema/SHA256SUMS.txt` holds 24 checksums: the three TL inputs, the generator, `api.h` and the 19 implementation files.
  `tools/check_schema.sh` verifies them and rejects stale implementation files.
- The schema provenance conflict (a header citing tdesktop and TDLib, and `NOTICE` citing Kurigram LGPL-3.0) is documented
  and not resolved. See `schema/README.md` and item 48.
- The Bot API generator is not in this repository. The Bot API method table is not reproducible from this tree (item 50).

## CI, release and cloud automation

Nothing was executed. Every job below is defined and checked for YAML validity.

`ci.yml` (on push and pull request, `permissions: contents: read`):
- `build-gcc`: Release build with tests on, GCC, single job, `ctest`.
- `build-clang`: Clang 18 build with tests.
- `build-arm64`: ARM64 build, runs only when `ENABLE_ARM64_CI` is `true`.
- `checks`: source list, version, format allowlist, API index, headers, example syntax, and schema reproducibility with
  the generator built from source.
- `sanitizers`: ASan and UBSan with the same flags as the Makefile.
- `thread-sanitizer`: `tools/run_tsan.sh`.
- `fuzz-smoke`: `tools/run_fuzz_long.sh` for 60 seconds.
- Logs are uploaded only on failure, with 7-day retention.

`release.yml` (manual `workflow_dispatch` only): fast checks that fail the run when they fail, a schema reproducibility step,
`make package`, `verify-release`, and an upload of the zip as an artifact with 14-day retention.

`live.yml` (manual `workflow_dispatch` only): reports SKIPPED unless `TG_API_ID`, `TG_API_HASH` and `TG_BOT_TOKEN` are set.
Values are never printed. It builds and runs the live check only when the secrets are present, and removes session and
database files in an `always()` step. It uploads no artifacts and has a 30-minute job timeout.

## Packaging

`tools/package_release.py` builds the source zip with a fixed timestamp and sorted entries. It refuses to continue when a
required file is missing. It excludes build directories, object files, binaries, session files, databases, logs, keys,
`compile_commands.json`, `.env` files other than `.env.example`, caches, IDE state and `dist/`. It writes `SHA256SUMS.txt`
into the archive only. The root-level copy is not bundled.

`tools/verify_release.sh` checks the zip integrity, that it has one top folder, the bundled checksums, that the file list
matches the checksum file, the required files, and that no forbidden path is present. It prints VERIFY PASSED or FAILED.

## API documentation

`docs/API.md` covers building and linking, creating a client and signing in, handling updates, messages and media,
asynchronous operations and cancellation, errors, the Bot API layer, and what is not in the API. `docs/API_INDEX.md` lists
the 470 `cppgram::Client` member names. The index is generated and checked against `client.h`. The check confirms each
name is defined. It does not check signatures or behaviour. Schema-blocked and unsupported items are marked in
`PARITY_MATRIX.md` and in the guide.

## Historical evidence

The final source pass ran 624 offline test cases on GCC 13.3 Release. They passed in six binaries, each run once with exit
code 0. Per-binary counts are in `TEST_RESULTS.md`. This was not rerun in the pre-VPS pass, because no test source changed
in this pass. Its build measurements are in `BUILD_RESULTS.md`. The earlier Valgrind run covered three subsets and is
labelled historical.

## Owner next steps

1. Review `NOTICE` and the schema provenance (items 48 and 49) before any public release.
2. Decide whether to enable the ARM64 CI job (`ENABLE_ARM64_CI`).
3. Run `make checks`, `make package` and `make verify-release` locally if you want to repeat the local checks.
4. Run the VPS and live steps in `VPS_TESTING_GUIDE.md` (E to Q) and record the results there. The live workflow is manual.

## Remaining limitations

See `KNOWN_LIMITATIONS.md`. The main ones for this release are: live Telegram and VPS behaviour not run; ARM64, Clang as a full
build, cross builds and Windows not verified; sanitizers and Valgrind not rerun in this pass; the format allowlist covers 53
of 243 files; the Bot API generator is not in the repository; the schema provenance and NOTICE need owner review; workflows
were never executed; the API index checks names, not behaviour; example programs are compile-checked only.
