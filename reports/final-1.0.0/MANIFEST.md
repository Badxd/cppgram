# Verification manifest: CppGram 1.0.0 final local run

Every row is a command that ran on the final `cppgram/` tree. The logs are in `logs/`, the scripts in `scripts/`. Times
are UTC. `status.txt` and `extra-status.txt` are the raw stage records written by the scripts.

## Stage records

Source: `status.txt` (the pipeline `run_all.sh`).

| Stage | Start | End | Exit | Notes |
|---|---|---|---|---|
| s0 GCC 13.3 Release: build, ctest, install, consumer | 19:22:26 | 19:26:03 | 0 | |
| s1 Clang 18.1.3 Release: build, ctest, install, consumer | 19:26:03 | 19:30:08 | 0 | |
| s2 schema check (`tl-check`) | 19:30:08 | 19:30:08 | 0 | |
| s3 TSan on bot, security, metrics, media | 19:30:08 | 19:34:06 | 0 | |
| s4 fuzz build and runs (first attempt) | 19:34:06 | 19:50:31 | 3 | Seed writer failed to link. Superseded: see below. |
| s5 clang-tidy-18 | 19:50:31 | 20:22:38 | 0 | 304 warnings |
| s6 Doxygen 1.9.8 | 20:22:38 | 20:23:28 | 0 | 899 warnings |
| s7 `make checks` | 20:23:28 | 20:25:23 | 0 | 21 OK lines |
| s8 live default mode (gated) | 20:25:23 | 20:25:53 | 0 | 4 PASS, 9 BLOCKED, 3 PENDING | Superseded by the network-gate fix; see `TEST_RESULTS.md` section 8.

## Later stages

Source: `extra-status.txt` and the fuzz summary `logs/s4-fuzz-summary.txt`.

| Stage | Exit | Result | Log |
|---|---|---|---|
| Fuzz, second attempt (libFuzzer rejected trace-pc-guard flags) | not recorded | Failed at start, rc=1 for all seven targets. Superseded. | logs overwritten |
| Fuzz, third attempt (`scripts/run_fuzz.sh`, fixed flags): build 71 of 71 | 0 | build finished | `logs/s4-build.log` |
| Fuzz campaigns, 7 × 120 s | 0 each, `FUZZ DONE fail=0` | 36,962,006 executions, 0 findings | `logs/s4-fuzz-<target>.log`, `s4-fuzz-summary.txt` |
| TSan `cppgram_async_test` | 0 | 46 passed, 0 reports | `logs/x-tsan-cppgram_async_test.log` |
| TSan `cppgram_mtproto_test` | 0 | 457 passed, 0 reports | `logs/x-tsan-cppgram_mtproto_test.log` |
| ASan+UBSan full build, first attempt (`-j2`) | 2 | `api_impl_2.cpp.o` failed with no diagnostic. | `logs/x-asan-build.log` |
| ASan+UBSan standalone recompile of `api_impl_2.cpp` | 0 | compiled | `logs/x-asan-api_impl_2.log` |
| ASan+UBSan full build, resumed serially (`-j1`) | 0 | 163 of 163 | `logs/x-asan-build-resume.log` |
| ASan+UBSan `ctest` | 0 | 711 of 711, 116.24 s; findings 0 | `logs/x-asan-ctest.log`, `x-asan-findings.txt` |

## Live project

| Run | Exit | Result | Log |
|---|---|---|---|
| `livebot_unit_tests` | 0 | 136 checks, 0 failures | `live/livebot-unit.log` |
| `cppgram_livetest` default mode | 0 | 4 PASS, 9 BLOCKED, 3 PENDING, `telegram_contact=no` on all | `live/livetest-default-mode.log`, `.json` | Superseded: the current default run is 4 PASS, 12 BLOCKED, 0 PENDING (`TEST_RESULTS.md` section 8).
| `CGLT_NETWORK_UNRESTRICTED=1 cppgram_livetest --category live_no_credentials` | 0 | 6 BLOCKED, `telegram_contact=no` on all | `live/livetest-override-interception.log`, `.json` |

## Documentation and check reruns

| Stage | Exit | Log |
|---|---|---|
| `make checks` after the documentation rewrite | 0, 21 OK | `logs/s7b-make-checks-after-docs.log` |
| Doxygen rerun with output in the build tree (`doc_doxygen`) | 0, 899 warnings | `logs/s6b-doxygen.log`, `s6b-configure.log` |

## Superseded records (not evidence)

Earlier rounds on earlier trees are in `../superseded/`. The first pipeline's fuzz logs, the failed first fuzz attempts,
and the first ASan build attempt were overwritten by later runs of the same stage. Their outcomes are recorded above
from the stage records, not from logs.

## Reproduce

Run the scripts from `scripts/` against the same tree. They write to `verify/` next to the source tree. Paths inside
the scripts refer to the build host and must be changed for another machine.
