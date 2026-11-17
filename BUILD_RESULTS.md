> **Historical.** This file was written for an earlier pre-release round (0.1.0). It is kept for the record. The current status is in `README.md`, `KNOWN_LIMITATIONS.md` and `FINAL_V1.0.0_READINESS_REPORT.md`.

# Build results: CppGram 0.1.0 (release-verification round, baseline 1cd09d0)

Every result below was produced by a command in this round. Log paths are relative to the evidence package root (the
folder that holds `FINAL_REPORT.md`). Exit codes come from the shell and are recorded in the log files.

## Environment

| Item | Value | Source |
|---|---|---|
| OS | Ubuntu 24.04.5 LTS, kernel 6.18.44 | `logs/bot-and-tools/environment.txt` |
| GCC | 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) | `logs/bot-and-tools/environment.txt` |
| Clang | 18.1.3 (Ubuntu 1ubuntu1), `clang++-18` | `logs/bot-and-tools/environment.txt` |
| CMake | 3.28.3 | `logs/bot-and-tools/environment.txt` |
| OpenSSL | 3.0.13 | `logs/bot-and-tools/environment.txt` |
| Host | 2 vCPU, 8 GB RAM, no GPU | `nproc`, `free` at run time |

Build directories were created outside the source tree (scratch area) and are not part of the release package.

## Clean builds (Release, C++20, tests enabled)

| Build | Configure | Build | Warnings | Errors | Logs |
|---|---|---|---|---|---|
| GCC 13.3.0 (`g++`) | exit 0 | exit 0 (`BUILD=0`) | 0 | 0 | `logs/gcc-configure.log`, `logs/gcc-build.log` |
| Clang 18.1.3 (`clang++-18`) | exit 0 (`CFG=0`) | exit 0 (`BUILD=0`) | 0 | 0 | `logs/clang-configure.log`, `logs/clang-build.log` |

Configure flags, GCC: `cmake -S . -B <dir> -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release -DCPPGRAM_USE_CCACHE=OFF`.
Clang: the same with `clang-18` and `clang++-18`. Build command: `cmake --build <dir> -j2`.

Warning and error counts are the number of lines containing `warning:` or `error:` in the build log. The two `error`
matches seen in a plain text search are file names (`errors.cpp`, `errors_test.cpp`) and are not diagnostics.

## Test binaries built and run

Six test binaries were built by each clean build. Each was run from its own tree; exit code 0 for all twelve runs.

| Binary | GCC run | Clang run | Logs (GCC / Clang) |
|---|---|---|---|
| cppgram_mtproto_test | 457 PASS, exit 0 | 457 PASS, exit 0 | `logs/cppgram_mtproto_test-clean-gcc.log` / `-clean-clang.log` |
| cppgram_async_test | 46 PASS, exit 0 | 46 PASS, exit 0 | `logs/cppgram_async_test-clean-*.log` |
| cppgram_bot_test | 110 PASS, exit 0 | 110 PASS, exit 0 | `logs/cppgram_bot_test-clean-*.log` |
| cppgram_media_test | 22 PASS, exit 0 | 22 PASS, exit 0 | `logs/cppgram_media_test-clean-*.log` |
| cppgram_metrics_test | 23 PASS, exit 0 | 23 PASS, exit 0 | `logs/cppgram_metrics_test-clean-*.log` |
| cppgram_security_test | 53 PASS, exit 0 | 53 PASS, exit 0 | `logs/cppgram_security_test-clean-*.log` |

## CTest (full suite, both compilers)

- GCC: `ctest -j2 --output-on-failure --timeout 900` → `100% tests passed, 0 tests failed out of 711`, exit 0 (`CTEST=0`), 68.25 s. Log: `logs/ctest-gcc.log`.
- Clang: the same command on the Clang tree → `100% tests passed, 0 tests failed out of 711`, exit 0 (`CTEST=0`), 69.52 s. Log: `logs/ctest-clang.log`.

## Other build and install checks

| Check | Command | Result | Log |
|---|---|---|---|
| Quality gate | `make checks` (source-check, version-check, format-check, api-check, check-examples) | exit 0 | `logs/bot-and-tools/make-checks.log` |
| API index | `python3 tools/api_index.py --check` | exit 0, 476 distinct Client members | `logs/bot-and-tools/make-api-index-check.log` |
| Version and changelog | `bash tools/check_version.sh` | exit 0 | `logs/bot-and-tools/check-version.log` |
| Install | `cmake --install <gcc tree> --prefix <scratch>` | exit 0 | `logs/bot-and-tools/install.log` |
| Consumer via `find_package` | build and run `consumer` against the installed prefix | configure 0, build 0, run 0 | `logs/bot-and-tools/consumer-*.log` |
| Live-offline wiring | `make live-offline-check LIVE_BUILD_DIR=<scratch>` (no Telegram contact) | exit 0, "all offline checks passed" | `logs/bot-and-tools/make-live-offline-check.log` |
| Public live-test bot | unmodified repo built against current library, ctest | configure 0, build 0, ctest 1/1 pass | `logs/bot-and-tools/bot-compat-public-*.log` |
| Release source archive | `python3 tools/package_release.py` then `tools/verify_release.sh` | see FINAL_REPORT.md | `logs/bot-and-tools/make-package-final.log` |

## Not built or not run

| Item | Status | Reason |
|---|---|---|
| ARM64 build | NOT RUN | No ARM64 host in this environment. |
| Windows | Unsupported | Not a supported platform (`docs/PORTABILITY.md`). Not attempted. |
| Documentation build (`BUILD_DOCUMENTATION`, Doxygen) | NOT RUN | Not part of this round's scope. |
| actionlint on `.github/workflows` | NOT RUN | Tool not installed in this environment. |
| Repo sanitizer and fuzz scripts (`tools/run_fuzz_long.sh`, CI sanitizer jobs) as scripts | NOT RUN | Equivalent builds and runs were done by hand (see TEST_RESULTS.md). |
| Schema regeneration (`make schema-check`) | NOT RUN in this round | The checked-in files were not regenerated. |
