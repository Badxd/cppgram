# Production readiness status

This page records what is verified and how. Each feature has one of these labels:

- **IMPLEMENTED**: code exists in the tree.
- **OFFLINE TESTED**: unit or fake-server tests pass locally. No real Telegram traffic.
- **CI VERIFIED**: the same tests pass in a GitHub Actions run on the release commit.
- **LIVE TESTED**: exercised against Telegram servers, a real proxy, or a real account.
- **BLOCKED**: a live test was not run, with the reason given.
- **NOT VERIFIED**: no test covers the behavior.

Nothing on this page is LIVE TESTED unless a run log says so. The project had no authorized test account or proxy in the
environment that produced this status.

## Feature status

| Area | Status | Evidence | Notes |
|---|---|---|---|
| Reconnect after drop | IMPLEMENTED, OFFLINE TESTED | `tests/mtproto/client_fake_dc_test.cpp`: unexpected drop, repeated drops, budget exhaustion, stop during backoff, stop prevents reconnect, concurrent stop and drop stress | Bounded by `ClientOptions::max_reconnects` (default 5), exponential backoff with equal jitter. Budget resets after a successful reconnect. LIVE TESTED: BLOCKED (no account). |
| FLOOD_WAIT | IMPLEMENTED, OFFLINE TESTED | Same file: retry after server delay, delay beyond policy returned without sleeping, bounded attempts, stop cancels the sleep. `session_test.cpp` covers cancellable sleep without resending. | Tested against a fake DC that returns `FLOOD_WAIT_n`. No real rate limit was triggered. LIVE TESTED: BLOCKED. |
| SOCKS5 | IMPLEMENTED, OFFLINE TESTED | `tests/mtproto/connection_net_test.cpp`, `socks5` tests: CONNECT, username/password (RFC 1929), IPv4 and domain targets, connect timeout, refused proxy | Domain targets are sent to the proxy as a domain name (ATYP 0x03), so the proxy resolves them. IPv4 and IPv6 literals are sent in binary. LIVE TESTED: BLOCKED (no authorized proxy). |
| Encrypted session file (CGE1) | IMPLEMENTED, OFFLINE TESTED | `SessionCrypto*` tests: round trip, wrong passphrase, tampered ciphertext, truncation, out-of-range scrypt cost | AES-256-GCM, scrypt N=2^15, r=8, p=1. Header is authenticated as AAD. Passphrase is read from `CPPGRAM_SESSION_PASSPHRASE`. LIVE TESTED: BLOCKED. See [SESSION_ENCRYPTION.md](SESSION_ENCRYPTION.md). |
| Obfuscated MTProto proxy (client) | IMPLEMENTED, OFFLINE TESTED | `tests/mtproto/obfuscated_test.cpp`, `connection_net_test.cpp` (`ObfuscatedNet.*`) | Plain secrets (32 hex characters) and padded-intermediate secrets (`dd` followed by 32 hex characters). Fake-TLS (`ee`) secrets are rejected with `std::invalid_argument`. Handshake was cross-checked against two independent open-source implementations, not against Telegram servers. Server-to-client padded framing: NOT VERIFIED. LIVE TESTED: BLOCKED. |
| Connect and I/O timeouts | IMPLEMENTED, OFFLINE TESTED | `ConnectionNet.*` tests | `connect_timeout_ms` and receive/send timeouts. |
| Cancellation and shutdown | IMPLEMENTED, OFFLINE TESTED | `CancelStress*`, `Cancel*` tests | `Client::stop()` is safe from any thread. A new `run()` starts unstopped, by design. |
| Thread safety | OFFLINE TESTED (TSan) | Full `cppgram_mtproto_test` under ThreadSanitizer: 237 tests passed, 0 warnings | Fixes in this round: `is_connected()` now reads an atomic flag. Handlers are cleared in `~Client()`. |
| Memory and UB | OFFLINE TESTED (ASan, UBSan) | `tools/run_sanitizers.sh`: 316 of 316 tests passed, 0 sanitizer reports | |
| Fuzzing | OFFLINE TESTED | See the fuzz section below | |
| GitHub Actions | CI workflow written; NOT VERIFIED on GitHub | `.github/workflows/ci.yml`, `.github/workflows/live.yml` | The push to the remote was refused (HTTP 403), so no workflow has run on GitHub for the release commit. |

## Running the checks locally

```
make test                       # configure, build, run the unit and offline suite
bash tools/run_sanitizers.sh    # ASan and UBSan, build-san/
bash tools/run_tsan.sh          # ThreadSanitizer, build-tsan/ (slow: -j1)
bash tools/run_fuzz_long.sh 600 # clang-18 libFuzzer targets, build-fuzz/
bash tools/check_headers.sh     # each public header compiles alone
bash tools/check_schema.sh      # TL schema reproduces from source
bash tools/check_examples.sh    # examples compile
```

## Live tests

`examples/live_check.cpp` reads `TG_API_ID`, `TG_API_HASH` and `TG_BOT_TOKEN` from the environment. It does not write them to
disk. `.github/workflows/live.yml` runs only when started by hand and only when those repository secrets are set.

Live checks that have not been run: user login, reconnect against Telegram, FLOOD_WAIT against Telegram, SOCKS5 to a real
proxy, obfuscated proxy against a real proxy, and encrypted sessions with a real account. Each is BLOCKED until someone with
an authorized account or proxy runs it. Results must be recorded with the commit SHA and date.

## Known limitations

- Obfuscated proxy: fake-TLS secrets are not supported. Server-to-client padded framing is not verified.
- No live verification of any Telegram interaction in this round.
- `Client::stop()` before `run()` is ignored by design. Call `stop()` after `run()` has started.
- Session encryption protects the file at rest. It does not protect memory, and it does not help if the passphrase is stored
  next to the file.

## Security notes

- Never commit session files, passphrases, API hashes, bot tokens or private keys. The `.gitignore` covers `build-*/`.
- Credentials for live tests come only from the environment or GitHub Secrets.
- Proxy secrets are parsed and validated before any network I/O.
