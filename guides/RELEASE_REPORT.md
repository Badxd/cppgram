# Release report: verification status

Scope: the cppgram tree at the HEAD named in the final zip. This is a status report, not a production-readiness claim.
Each result below names the environment it was measured in. Nothing here was run on Telegram's live servers.

## Verdict

Not production-ready. Live Telegram behaviour (login, reconnect, flood waits, SOCKS5 through a real proxy, encrypted
sessions against a real account) has not been verified. Until those pass on a real account, this library is a
tested component set, not a proven client.

## Test results (sandbox: Ubuntu 24.04, GCC 13.3, 2 vCPU, 8 GB RAM, OpenSSL 3.0.13)

| Check | Command | Result |
|---|---|---|
| Release unit + integration suite | `ctest -j2` (Release) | 267/267 pass, 0 compiler warnings |
| ThreadSanitizer, test binary | `cppgram_mtproto_test` built `-O0 -fsanitize=thread`, -j1 | 188/188 pass, 0 warnings (after fix in 03073a1) |
| ThreadSanitizer before fix | same | 1 failure: a stop request could be missed by a waiter; fixed |
| libFuzzer + ASan, TL reader | 500 s | 11.7M runs, 0 crashes |
| libFuzzer + ASan, HTML parser | 500 s | 7.2M runs, 0 crashes |
| libFuzzer + ASan, session string | 100 s | 66.9M runs, 0 crashes |
| ASan + UBSan, test binary | see `san_build.log` / run log | filled in below if completed |

Fuzzing used ASan only, not UBSan, and ran for minutes rather than hours.

## Implemented (with tests)

- MTProto handshake with DH validation, message keys, AES-256-IGE, replay defence for server messages (F16)
- Abridged and intermediate transports, framing and malformed-frame tests
- Auth state machine with RPC error mapping (13 tests)
- Retry policy, bounded reconnect budget, cancellable sleep (3 + 2 stress tests)
- Session storage: CGS2 file with CRC, SQLite store with integrity check and schema version
- Encrypted sessions: file, SQLite key column, session string. scrypt + AES-256-GCM, passphrase from env (`CPPGRAM_SESSION_PASSPHRASE`). Documented in `guides/SESSION_ENCRYPTION.md`
- SOCKS5 CONNECT with username/password auth, IPv4, IPv6, domain targets, timeouts (13 tests, loopback proxy)
- SOCKS5 wired into the main DC connection (`ClientOptions::socks5`), tested against a loopback proxy only
- Update gate: account pts, per-channel pts (in memory), gap recovery via getDifference / getChannelDifference
- Typed events: connection state, auth state, RPC errors. Handler exceptions are isolated
- Uploads: parts streamed from disk, MD5 computed in a streaming pass; plan limits enforced
- Downloads: written to `<path>.part`, resumed from an aligned offset, atomic rename on success

## Explicitly incomplete

- Live verification: login, reconnect, flood waits, SOCKS5 through a real proxy, encrypted session against a real account
- MTProto obfuscated proxy: design only (`guides/OBFUSCATED_PROXY_PLAN.md`). No code
- Per-channel pts is in memory only. After restart the first update per channel is trusted again
- Download resume trusts the `.part` file by size. Telegram gives no hash for these files
- `export_session_string()` and SQLite store are encrypted only when the passphrase env var is set
- Passphrase rotation is not implemented
- CDN downloads connect directly, not through the proxy
- Typed events cover connection, auth state, and RPC errors only. Message-level typed events are the existing handler API
- Kurigram-level feature parity is out of scope for this library release

## Known risks

- Never tested against a live Telegram DC from this environment
- scrypt cost (N=2^15) makes each encrypted load or save take about 100 ms
- A stolen authorized session gives account access. Encryption at rest does not protect a running process
- CI results for the current HEAD were observed by the maintainer, not reproduced here
