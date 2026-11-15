# Known limitations: CppGram 0.1.0 (release-verification round, baseline 1cd09d0)

Status values: PASS, FAIL, BLOCKED, NOT OBSERVED, NOT RUN, PENDING USER APPROVAL. Nothing below is marked PASS unless it
was run and checked in this round.

## Release-blocking and approval items

| ID | Item | Status | Owner action |
|---|---|---|---|
| KL-1 | Rotation of the VPS and live-test credentials | PENDING USER APPROVAL | Owner decides. Nothing was rotated, and no live service was touched. |
| KL-2 | telemt proxy public 8443, classic and secure modes | PENDING USER APPROVAL | Owner decides whether to keep it public. Not changed. |
| KL-3 | Binary `bin/cppgram_live_test_bot` committed in the public Cpplivetest repository | PENDING USER APPROVAL | Remove from tracking and add `bin/` to `.gitignore`. See `cpplivetest-review.md`. Not changed here. |

## Not verified against Telegram

- Live Telegram tests (`cppgram_live_check`, `cppgram_live_tests`, and the six live cases): NOT RUN. No credentials
  were used. No live login, session, reconnect, socks5, proxy or padded-framing result is claimed.
- Live FLOOD_WAIT: NOT OBSERVED. Offline FLOOD_WAIT tests pass (fake DC), but they do not prove the server behaviour.
- Public Cpplivetest bot run against Telegram: NOT RUN. The bot builds and its unit test passes against the current
  library, which is all that was checked.

## Network

- Offline simulated drop recovery passes on a fake DC. Actual interruption on a real network path (packet loss or
  socket reset) is NOT RUN. Live drop against Telegram is NOT RUN.
- Proxy coverage: plain and dd MTProxy modes are supported and tested offline. Fake-TLS (`ee`) secrets are refused by
  design. That is a deliberate limit, not a bug.

## Fuzzing

- Campaigns are short: 241 s per target for the three instrumented harnesses, 61 s for the repo targets. Crash-freedom
  in these runs is not evidence of absence of defects.
- NOT RUN: long corpus runs (`tools/run_fuzz_long.sh`), `fuzz_session_string`, `fuzz_html_parser`,
  `fuzz_session_crypto`.
- NOT RUN: the malformed-padding-grammar sub-case. Its coverage is a documented gap.

## Sanitizers

- ASan+UBSan and TSan were run on targeted subsets of the mtproto binary only (118 and 47 tests). Other binaries (async,
  bot, media, metrics, security) and the full suite were NOT RUN under sanitizers in this round.
- Earlier-round sanitizer counts are not reproduced and are not claimed.

## Standards and parity

- TDLib parity of the plain trailing-byte handling: BLOCKED. The upstream source could not be fetched through the
  allowed fetch path. The decision in `PROXY_AUDIT.md` section 7.2 is based on the spec and on CppGram's own tests, and
  is not verified against TDLib.
- Kurigram and other reference libraries: classification only (`PARITY_MATRIX.md`). No behavioural parity is claimed.

## Platforms and build

- Verified: Linux x86_64, GCC 13.3.0 and Clang 18.1.3, Release, C++20.
- NOT RUN: ARM64.
- Unsupported: Windows.
- NOT RUN: Doxygen documentation build, actionlint on the CI workflows, and the repo sanitizer and fuzz scripts as
  scripts (equivalent runs were done by hand).
- NOT RUN in this round: schema regeneration (`make schema-check`). The checked-in TL-generated files were not
  regenerated.

## Test-count history

- The earlier checkpoint numbers (700 CTest, 446 MTProto) are 2 below the pre-mission tree. The two tests are not
  identifiable from preserved artifacts. See TEST_RESULTS.md section 1.

## Repository state

- No commits, tags, branches or pushes were created in this round. HEAD is 1cd09d0 before and after.
- The repository's tracked `SHA256SUMS.txt` and `commit-history.txt` were regenerated for this release. The earlier
  `commit-history.txt` was a hand-written local change log, not git history. It was replaced.
