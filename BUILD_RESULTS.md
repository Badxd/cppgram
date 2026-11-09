# Build results

All numbers here come from commands run in the release session. Commands and logs are named so they can be checked.
Nothing here was run on the VPS, on ARM64, or against Telegram.

This file has three parts. The first two are the measurements that led to the build change in section 3. Section 4 is
what was run on the tree as delivered.

## 1. Machine

| Item | Value |
|---|---|
| Architecture | x86_64 |
| OS | Ubuntu 24.04.5 LTS |
| RAM | 8022 MB (about 8 GB) |
| Cores | 2 |

No 1 GB machine was used. See section 6.

## 2. Toolchain and dependencies

| Tool | Version | Used for |
|---|---|---|
| GCC | 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) | Release build and all test runs |
| Clang | 18.1.3 (Ubuntu 1ubuntu1) | `-fsyntax-only` on changed files only. Not used to build or run anything |
| CMake | 3.28.3 | configure and build driver (project minimum is 3.16) |
| Ninja | 1.11.1 | generator |
| mold | used as the fast linker (`CPPGRAM_FAST_LINKER=auto` picked it) | link step |
| Boost | 1.83.0 | found at configure |
| libcurl | 8.5.0 | found at configure |
| nlohmann_json | 3.11.3 | found at configure |
| OpenSSL | 3.0.13 | found at configure |
| SQLite | 3.45.1 | found at configure |
| GoogleTest | 1.14.0 | found at configure |
| zlib | 1.3 | found at configure |
| Valgrind | 3.22.0 | memcheck, subset of suites (section 4g) |

Configure used: `cmake -S . -B build-clean -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DCPPGRAM_USE_CCACHE=OFF`.
`BUILD_LIVE_TESTS`, `BUILD_LIVE_CHECK`, `ENABLE_SANITIZERS` and `BUILD_FUZZERS` were left off.

## 3. Build memory: measurements and the change made

### 3a. Starting point (350 function bodies per generated file)

The generator (`tools/tl_codegen/tl_codegen.cpp`) packed 350 function bodies into each `api_impl_N.cpp`, which gave 10
generated files. One of them, `api_impl_8.cpp`, is the largest single compile.

Measured in this pass, one compile at a time, with the compile-database command line (`-O3 -DNDEBUG -std=c++20 -Wall`),
ccache off, nothing else running:

| Variant | Peak RSS (KiB) | Compile wall (s) | Notes |
|---|---:|---:|---|
| `api_impl_8.cpp`, `-O3` (350 per file) | 4,050,028 | 61.2 | baseline |
| `api_impl_8.cpp`, `-O1` override (350 per file) | 3,868,648 | 55.7 | 4.5% lower; not adopted |

An `-O1` override for the generated files saves about 180 MB on the worst file. The peak stays above 3.8 GB, so it does
not change what a small machine needs. It was not adopted.

The earlier clean build recorded a single-process peak of 4,050,796 KiB for the same file, within 0.02% of the
re-measurement above.

### 3b. Smaller split (175 bodies per file)

The generator was built from a copy with `kPerFile = 175` and run into a scratch directory. That gave 19 files
(`api_impl_0.cpp` to `api_impl_18.cpp`). Each file was then compiled alone at `-O3`, serially, nothing else running.

| File | Peak RSS (KiB) | Wall (s) |
|---|---:|---:|
| api_impl_0 | 1,795,524 | 28.5 |
| api_impl_1 | 1,571,328 | 20.9 |
| api_impl_2 | 2,721,456 | 38.0 |
| api_impl_3 | 2,654,068 | 34.4 |
| api_impl_4 | 1,565,296 | 20.7 |
| api_impl_5 | 1,704,532 | 21.3 |
| api_impl_6 | 1,650,836 | 23.2 |
| api_impl_7 | 1,551,920 | 20.1 |
| api_impl_8 | 1,492,252 | 19.5 |
| api_impl_9 | 1,619,408 | 22.1 |
| api_impl_10 | 1,173,080 | 10.3 |
| api_impl_11 | 1,209,900 | 10.8 |
| api_impl_12 | 1,177,432 | 9.6 |
| api_impl_13 | 1,190,964 | 11.5 |
| api_impl_14 | 1,177,412 | 10.5 |
| api_impl_15 | 1,636,844 | 18.8 |
| api_impl_16 | 2,050,728 | 26.2 |
| api_impl_17 | **2,836,644** | 42.3 |
| api_impl_18 | 1,391,948 | 16.3 |

Largest single file: `api_impl_17.cpp` at 2,836,644 KiB (about 2.7 GiB). Serial compile wall time for the 19 files is
about 405 s.

### 3c. Decision

The 175-body split was adopted. The reasons, from the numbers above:

- It lowers the largest single compile from 4.05 GB to 2.84 GB, about 30%. The `-O1` override lowered it by 4.5%.
- Its cost is compile CPU. The 19 files take about 405 s serially. The per-file table of the earlier 10-file build (replaced by
  this file) summed to about 310 s, but it was measured with a different flag set (`-I include -I src`, no compile
  database), so that comparison is approximate. The extra cost comes from each translation unit
  parsing `api.h` again.
- Both variants leave the peak above 1 GB. Neither makes a 1 GB build realistic.

Changes made:

- `tools/tl_codegen/tl_codegen.cpp`: `kPerFile` changed from 350 to 175, with a comment.
- `src/mtproto/schema/`: `api_impl_0.cpp` to `api_impl_18.cpp` regenerated (19 files, was 10). `api.h` is unchanged.
- `CMakeLists.txt`: both lists that name the `api_impl` files (the source list and the unity-build exclusion list) now
  name 0 to 18.

Schema reproducibility, `tools/check_schema.sh`, run with the generator built from the repository source:

- `OK api.h`, `OK api_impl_0.cpp` through `OK api_impl_18.cpp`, `OK layer 233 (schema and client agree)`. Exit 0.
- Counts from the generator: 1773 types, 877 functions, 683 abstract types, 19 impl files.

Note on the build order: `cmake` cannot configure with the new file list until the new files exist, and the generator
binary lives in the build tree. The regeneration was done with a standalone build of `tools/tl_codegen/tl_codegen.cpp`,
then the build tree was reconfigured. A fresh checkout does not need this step; it is recorded here because it happened.

## 4. Build and test runs on the tree as delivered

### 4a. Clean build before the split (earlier in this release session)

| Measure | Result |
|---|---|
| Command | `cmake --build build-clean -j2` on a clean configure |
| Exit code | 0 |
| Steps | 176 |
| Wall time | 951.9 s |
| Compiler cache | off |
| Peak RSS of the largest single process | 4,050,796 KiB (the `api_impl_8.cpp` compile) |
| Warnings | 2 (`-Wunused-function`, fixed; an incremental rebuild then showed 0) |

This clean build predates the split. It is kept as the baseline in section 3.

### 4b. Rebuild after the split

| Measure | Result |
|---|---|
| Command | `cmake .` (reconfigure), then `cmake --build . -j2` in `build-clean` |
| Exit code | 0 |
| Steps | 110 |
| Warnings | 0 (`grep -c "warning:"` on the build log) |
| Scope | Incremental on an existing tree. It is not a second full clean build. |
| Wall time | not recorded for this rebuild |
| Peak RSS | the per-file numbers in section 3b. A whole-build peak was not measured after the split. |

Parallelism note: the rebuild ran at `-j2` on this 8 GB machine and finished without an out-of-memory kill. The two
largest generated files were not measured compiling together, so `-j2` is shown to work here, not to be safe on every
machine. On a 4 GB machine use `-j1`.

### 4c. Test runs (one run of each binary, in sequence)

Each binary ran once with `--gtest_output=xml`. Exit codes and peak RSS come from the wrapper.

| Binary | Exit | Cases | Passed | Peak RSS (KiB) |
|---|---:|---:|---:|---:|
| cppgram_bot_test | 0 | 110 | 110 | 18,016 |
| cppgram_mtproto_test | 0 | 370 | 370 | 51,488 |
| cppgram_security_test | 0 | 53 | 53 | 16,364 |
| cppgram_metrics_test | 0 | 23 | 23 | 15,836 |
| cppgram_async_test | 0 | 46 | 46 | 13,912 |
| cppgram_media_test | 0 | 22 | 22 | 62,560 |
| **Total** | | **624** | **624** | |

Per-file counts are in `TEST_RESULTS.md`. These are the test processes, not a measurement of the library in production.

### 4d. Link and install

| Measure | Result |
|---|---|
| `cmake --install build-clean --prefix <temp>` | exit 0 |
| Installed headers under `include/cppgram` | 55 |
| Installed CMake package files | 3 |
| Static library | `lib/libcppgram.a` |
| Text symbols in the library | 8,603. All are in the `cppgram` namespace (`nm -C --defined-only`). |
| Consumer `find_package` test | not run |

### 4e. Examples

`tools/check_examples.sh` (GCC 13.3, `-fsyntax-only -Wall` against the public headers): 17 of 17 OK, 0 FAIL. Examples are
compile-checked only. None was linked or run.

### 4f. Clang

`clang++` 18.1.3 `-std=gnu++20 -fsyntax-only -Wall` on the two test files changed in this pass,
`tests/mtproto/ephemeral_upload_fake_dc_test.cpp` and `tests/mtproto/rich_blocks_fake_dc_test.cpp`: exit 0, no
diagnostics. Earlier in the release, Clang also checked nine changed sources and tests. The whole tree was not built with
Clang.

### 4g. Valgrind memcheck

Earlier in this release, before the split and before the two tests added in this pass: `cppgram_security_test` (53
cases), `cppgram_metrics_test` (23 cases), and 34 cases of `cppgram_mtproto_test` in the subset `EphemeralInlineFakeDc.*`,
`InlineDecodeFakeDc.*`, `InlineSendFakeDc.*`, `KeyboardRoundTrip*`, `RichBlocksFakeDc.*` and `EphemeralUploadFakeDc.*`
ran under Valgrind with 0 errors and no leaks reported.

Not rerun in this pass. The two new tests (`EphemeralUploadFakeDc.CancelDuringUploadStopsBeforeSend`,
`RichBlocksFakeDc.TableCellSpansBelowOneAreRejected`) and the generated-file split have not been run under Valgrind. The
whole `cppgram_mtproto_test` binary has not been run under Valgrind.

### 4h. Sanitizers

NOT RUN. `tools/run_sanitizers.sh` builds the whole suite with ASan and UBSan at `-j1`. Its own header says that needs
about 8 GB and about 1.5 hours. TSan (`tools/run_tsan.sh`) was not run.

### 4i. ARM64

NOT RUN. This host is x86_64. No ARM64 configure, build or test was done.

## 5. Schema reproducibility

`tools/check_schema.sh`: see section 3c. The regenerated `api.h` and `api_impl_0.cpp` through `api_impl_18.cpp` match the
checked-in files. Layer 233 in `schema/main_api.tl` matches the layer the client sends.

## 6. Memory on a 1 GB VPS

NOT MEASURED. The measurements above are for an 8 GB, 2-core machine.

The largest single compile is now `api_impl_17.cpp` at about 2.8 GB. A 1 GB machine would have to compile that file with
several GB of swap, and that was not tried. The 1 GB result is therefore unverified, and the numbers here do not show that
it fits.

Recommendations, based on the measurements above and not on a run at these sizes:

- RAM: at least 4 GB for a `-j1` build of the library. The largest single compile needs about 2.8 GB, and the rest of the
  system needs room alongside it.
- Swap: if the machine has less RAM than the largest compile, add swap. The compile will be slow, and the result is not
  verified.
- Parallelism: `-j1` on machines below 8 GB. `-j2` ran on this 8 GB machine.
- Disk: not measured.

`VPS_TESTING_GUIDE.md` steps E, F, J and K describe how to measure a small machine.

## 7. Historical

`docs/historical/CPPGRAM_BUILD_MEMORY_REPORT.md` is an earlier build-memory report. It is superseded by this file.
