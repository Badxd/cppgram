# Review of the public live-test bot repository (Badxd/Cpplivetest)

Status of this review: the public repository was NOT changed. No commit, push, tag or deletion was made by this round.
The local patch and the binary-removal note are kept under `source/` and are not applied upstream.

## 1. Committed binary (finding SR-7)

| Item | Value |
|---|---|
| Path | `bin/cppgram_live_test_bot` |
| Size | 10,833,856 bytes (10.8 MB) |
| Format | ELF 64-bit LSB PIE, x86-64, dynamically linked, not stripped |
| Build ID | 387cbb35925592f6b1aadc24c70b01858da28ccd |
| SHA-256 | f634946c9b630746bbb009a974dd47987616b2b1b8b616c1c386237f1e8b3ffc |
| Added in | commit 3e7cc2c, 2026-11-14 13:11 UTC (author account Badhacker98) |
| Changed in | commit b8db65c, 2026-11-14 14:36 UTC |
| Ignored by | nothing: `.gitignore` does not list `bin/` |

Checks performed on the binary (no secret value was printed):
- Token-shaped strings (`<digits>:<35 chars>`): 0 matches.
- Session-string-shaped and PEM private-key headers: 0 matches.
- API-hash-shaped assignments and MTProxy-secret-shaped strings: 0 matches.
- Build-machine paths (`/home/`, `/tmp/claude`): 0 matches.

A pattern scan cannot prove the absence of a credential. It only shows that none of the common shapes is present.

## 2. Risk

- The binary is not reproducible from the repository by a reviewer. Its build inputs and compiler are not recorded.
- It can drift from the source. Nothing in the repository proves which source produced it.
- Anything compiled into it becomes public to everyone who can clone the repository.
- Removing the file from the current tree does NOT remove it from history. Anyone can still fetch commits 3e7cc2c and b8db65c.
- If a real credential was ever compiled into a published build, that credential must be treated as exposed and rotated. That is a decision for the owner and is PENDING USER APPROVAL.

## 3. Recommended owner action (not performed here)

1. Stop tracking the binary: `git rm --cached bin/cppgram_live_test_bot`, add `bin/` to `.gitignore`. A local patch with these changes is at `source/mission4/cpplivetest-docs-and-build-fixes.patch` and the removal note is at `source/mission4/cpplivetest-REMOVE-COMMITTED-BINARY.txt`. The patch does not delete the binary. Apply it, then delete the file in the same commit.
2. Publish the binary, if it is needed, as a release asset with a published SHA-256, built from a tagged commit.
3. Decide whether history must be rewritten to purge the binary. Only the owner can decide that, because it rewrites public history.
4. If the owner has any reason to believe a credential was compiled into a published build, rotate it. Rotation is PENDING USER APPROVAL and was not done.

## 4. Compatibility of the public bot with the current CppGram tree

- Build: the unmodified public repository was configured against the current CppGram headers (`work/include`) and the static library built from the current tree (clean GCC build). Configure exit 0, build exit 0. Logs: `logs/bot-and-tools/bot-compat-public-*.log`.
- CTest: 1/1 passed (`livebot_unit_tests`), exit 0.
- Live bot run against Telegram: NOT RUN. No credentials were used.

## 5. Secret-shaped scan of the public repository

Scanned 30 files (1 binary, the file above). Results (redacted):
- No token, session-string, PEM, GitHub-token or AWS-key shapes.
- `tests/unit_tests.cpp:54`: one MTProxy-shaped value. It is a sequential placeholder constant in a unit test, not a secret.
- `src/config.cpp:89-93`: six Telegram DC addresses (the same published DC table as the CppGram library). Not private infrastructure.

## 6. Documentation wording (low severity)

31 matches for token, session, VPS or API-hash wording across six public Markdown files (README.md 8, FINAL_REPORT.md 8, TESTS.md 5, TEST_RESULTS.md 6, LIBRARY_NOTES.md 3, COMMANDS.md 1). Read by line, they describe variable names (`TG_API_ID`, `TG_API_HASH`, `TG_BOT_TOKEN`) and behaviour. None contains a value.

Points for the owner to decide, not to act on here:
- `README.md` names a server path (`/var/lib/cppgram-livebot/`) and says the bot runs from a VPS. That is private deployment layout, not a secret. Remove it if the repository should not describe the host.
- An earlier broad search returned lines that appeared to come from third-party Playwright documentation. A recursive re-check of the repository tree found no such files (30 files, non-.git). Those lines are not part of this repository and were not reviewed further.
