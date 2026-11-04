# Production readiness status

Labels used in this project. Each one is a claim about the evidence, and no label means more than what is listed under it.

- **IMPLEMENTED**: the code exists and is reachable through the public API or the documented tools.
- **OFFLINE TESTED**: unit, protocol, fake-server or fake-DC tests pass. No Telegram traffic and no real proxy were used.
- **GCC VERIFIED**: the library and all test targets build with g++-12 (12.4.0) from a fresh build directory, and ctest passes.
- **CLANG VERIFIED**: the same, with clang++-18 (18.1.3).
- **SANITIZER VERIFIED**: the offline tests pass under AddressSanitizer with UndefinedBehaviorSanitizer, or under ThreadSanitizer, with the recorded results.
- **FUZZ TESTED**: the libFuzzer target ran for the recorded time and execution count without a crash or sanitizer report.
- **NOT VERIFIED LIVE**: never run against Telegram servers, a real proxy or a real account. The project owner will test these later on Heroku with an authorized environment.
- **BLOCKED**: a test that cannot run in this environment, with the reason given.
- **UNSUPPORTED**: deliberately not implemented. Such secrets or features are rejected before any network use.

Nothing on this page has been verified live. The environment that produced it had no authorized account, proxy or Telegram
credentials. No GitHub Actions run exists for this snapshot.

## Feature status

| Area | Labels | Evidence | Notes |
|---|---|---|---|
| Reconnect after drop | IMPLEMENTED, OFFLINE TESTED, NOT VERIFIED LIVE | `ClientFakeDc.*`, `ClientLifecycle.*` | Bounded by `max_reconnects`, jittered backoff, cancellable. See [RECONNECT_AND_FLOOD_WAIT.md](RECONNECT_AND_FLOOD_WAIT.md). |
| FLOOD_WAIT | IMPLEMENTED, OFFLINE TESTED, NOT VERIFIED LIVE | `ClientFakeDc.FloodWait*`, `ClientFakeDc.StopCancelsAFloodWaitInProgress` | Controlled fake DC only. No real rate limit was triggered. |
| Client lifecycle and handlers | IMPLEMENTED, OFFLINE TESTED, SANITIZER VERIFIED | `ClientLifecycle.*` (connect/disconnect cycles, concurrent stop, state reads during reconnect, no handler after destruction) | Handlers must be registered before `connect()` or `run()`. Bot login is not covered by the fake DC. |
| Session storage (SQLite) | IMPLEMENTED, OFFLINE TESTED, SANITIZER VERIFIED | `StorageConcurrency.*`, `Storage.*`, `SqliteEncrypted.*` | Calls are serialized per object. Two objects may share a file (WAL, busy timeout). |
| Encrypted session (CGE1) | IMPLEMENTED, OFFLINE TESTED, FUZZ TESTED, NOT VERIFIED LIVE | `SessionCrypto.*`, `SessionStringEncrypted.*`, `EncryptedSession.*`, `fuzz_session_crypto` | AES-256-GCM, scrypt with bounded cost, header as AAD. Session files are mode 0600. See [SESSION_ENCRYPTION.md](SESSION_ENCRYPTION.md). |
| SOCKS5 | IMPLEMENTED, OFFLINE TESTED, NOT VERIFIED LIVE | `Socks5.*`, `Socks5Connection.*`, `ConnectionNet.*` | No-auth and username/password. IPv4, IPv6 and domain targets. See [PROXIES.md](PROXIES.md). |
| MTProto obfuscated proxy, plain and padded secrets | IMPLEMENTED, OFFLINE TESTED, NOT VERIFIED LIVE | `Obfuscated*`, `ObfuscatedNet.*`, `ObfuscatedFrame.*` | Padded server frames are handled offline. Whether real servers pad is unknown. |
| Fake-TLS (`ee`) proxy secrets | UNSUPPORTED | `ObfuscatedNet.FakeTlsSecretIsRejectedBeforeConnecting` | Rejected before any connection. |
| Padded-intermediate framing | IMPLEMENTED, OFFLINE TESTED, NOT VERIFIED LIVE | `ObfuscatedFrame.*`, `Crypto.TrailingTransportPaddingIsIgnoredButExtraBlockIsRejected` | Client and server-to-client directions, fragmented reads, truncation, error words, padding overrun. |
| Thread safety | SANITIZER VERIFIED | TSan: 35 focused and 260 full tests, 0 warnings | See the results table below. |
| Memory and undefined behavior | SANITIZER VERIFIED | ASan and UBSan: 339 of 339 tests, no reports | See the results table below. |
| Build portability | GCC VERIFIED, CLANG VERIFIED | Fresh build directories for both compilers | C++20 is set on the library target. |
| Fuzzing | FUZZ TESTED | Results table below | Smoke-length runs. Not a long campaign. |
| CI workflows | IMPLEMENTED, NOT VERIFIED LIVE | `.github/workflows/ci.yml`, `live.yml` | Written, not executed on GitHub. |
| Live Telegram behavior | NOT VERIFIED LIVE | See the list below | Postponed to the project owner. |

## Verification results (final source)

Commands are in the README and in [fuzz/README.md](../fuzz/README.md). Exact commit hashes and dates are in
`commit-history.txt` in the package.

| Check | Environment | Result |
|---|---|---|
| Release build, fresh directory | g++-12 12.4.0, `-DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON` | Builds. Two false-positive warnings from GCC 12 were removed (`fake_dc.h`, `extra.cpp`). Final rebuild: 0 warnings. |
| ctest | g++-12 | 339 of 339 passed |
| Release build, fresh directory | clang++-18 18.1.3 | Builds. 0 warnings. Unused constants removed from `client.cpp`. |
| ctest | clang++-18 | 339 of 339 passed |
| ASan + UBSan | `tools/run_sanitizers.sh` | See the final run below. |
| TSan | `tools/run_tsan.sh` | 35 focused, 260 full, 0 warnings |
| Header self-containment | `tools/check_headers.sh` | Exit 0 |
| Example syntax | `tools/check_examples.sh` | Exit 0 |

Fuzzing, all with clang-18 libFuzzer and ASan. Every run exited 0 with no crash, timeout or sanitizer report.

| Target | Corpus | Duration | Executions | Coverage (edges) |
|---|---|---|---|---|
| `fuzz_tl_reader` | none (no TL seed generator in the repo) | 601 s | 16,749,453 | 30 |
| `fuzz_session_string` | none | 601 s | 446,703,929 | 13 |
| `fuzz_session_string` | 8 valid session strings | 301 s | 106,432,734 | 14 |
| `fuzz_html_parser` | none | 601 s | 10,908,327 | 28 |
| `fuzz_session_crypto` | none | 601 s | 15,812,122 | 5 (most inputs fail the magic check) |
| `fuzz_session_crypto` | 4 valid and 11 invalid CGE1 seeds | 601 s | 21,385 | 16 |

The CEG1 numbers are low on purpose. Each decryption that gets past the header runs scrypt (about 30 ms), so the seeded
run covers about 35 inputs per second. Coverage is limited to one passphrase and to the code reached through the seeds.

## Live tests: postponed to the project owner

Each item is **NOT VERIFIED LIVE — USER WILL TEST LATER**:

- Real Telegram reconnect.
- Real Telegram FLOOD_WAIT.
- SOCKS5 connection to Telegram.
- Encrypted session restore with a real Telegram account.
- Real MTProto obfuscated proxy compatibility.
- Real Telegram padded-intermediate server framing compatibility.

GitHub Actions and any push are also out of scope for this snapshot.

## Known limitations

- No live verification of any Telegram interaction.
- Obfuscated proxy: fake-TLS (`ee`) secrets are UNSUPPORTED. Whether real servers pad their frames is unknown.
- `fuzz_tl_reader` has no seed corpus in the repository. The session-string target is seeded only with valid inputs.
- CEG1 fuzzing is limited by scrypt cost. A deeper campaign needs a test-only build with lower cost, which this
  project does not provide.
- Handlers (`Client::on_*`) must be registered before `connect()` or `run()`. Registration during a run is a data race.
- Scrypt for encrypted sessions allocates up to 128 MiB while deriving a key, if the file asks for the maximum cost.
- Results come from one machine. No CI run exists for this snapshot.
- `Client::stop()` before `run()` is ignored by design.

## Running the checks

    CXX=g++-12 CC=gcc-12 cmake -S . -B build/gcc -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON
    cmake --build build/gcc -j2 && ctest --test-dir build/gcc --output-on-failure
    CXX=clang++-18 CC=clang-18 cmake -S . -B build/clang -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON
    cmake --build build/clang -j1 && ctest --test-dir build/clang --output-on-failure
    bash tools/run_sanitizers.sh
    bash tools/run_tsan.sh
    bash tools/run_fuzz_long.sh 600
    bash tools/check_headers.sh
    bash tools/check_examples.sh

## Security notes

- Never commit session files, passphrases, API hashes, bot tokens or private keys. `.gitignore` covers build directories.
- Session files are created with mode 0600.
- Proxy secrets and SOCKS5 credentials are never logged. Error messages do not include them.
- The full review and its findings are in [SECURITY_AUDIT.md](SECURITY_AUDIT.md).
