# Changelog

Versions follow [Semantic Versioning](https://semver.org/). Until 1.0.0, a minor version may change the public API.

## 0.1.0 - first pre-release candidate (2026-11-09)

Basis for 0.1.0: no version has been tagged or published before, and the client has always announced `0.1.0` to Telegram
(`app_version`). The live Telegram path has not been verified, so the release is pre-1.0. `VERSION` is the single source;
`tools/check_version.sh` checks the other places that state it.

This release folds the earlier "Unreleased" sections below into one version. Their entries are kept for history.

Changes in 0.1.0 relative to the work before this release:

- Release preparation: `VERSION`, `RELEASE_NOTES.md`, the reproducible packager (`tools/package_release.py`) and its
  verifier (`tools/verify_release.sh`), `make checks`, `make package`, `make verify-release`.
- CMake: the version comes from `VERSION`; the package now ships `cppgramConfigVersion.cmake`; the package config
  finds OpenSSL, ZLIB and SQLite3, which a static-library consumer needs.
- Checks: `tools/check_cmake_sources.py` (every source and test file is built), `tools/check_version.sh`,
  `tools/format_check.sh` (clang-format-18 on 53 allowlisted files), and schema checksums in `schema/SHA256SUMS.txt`
  checked by `tools/check_schema.sh`, which also detects stale generated files.
- Generated TL code split into 19 files of 175 bodies each (was 10 files of 350), which lowers the largest single compile
  from about 4.05 GB to about 2.84 GB peak RSS.
- Removed: the Heroku and Docker deployment files (`Dockerfile`, `.dockerignore`, `heroku.yml`, `app.json`), and the
  second env template `env.example` (merged into `.env.example`). Duplicate guides are now pointers.
- Documentation: `README.md`, `AGENTS.md`, `HOW_TO_UPDATE_TELEGRAM_API.md` and `HOW_TO_RELEASE.md` rewritten to match the
  repository. Status and limits are in `IMPLEMENTATION_STATUS.md` and `KNOWN_LIMITATIONS.md`.
- Test count is now 624 test cases (historical run, see `TEST_RESULTS.md`). The earlier count of 621 predates the typed
  rich-block test.

### Included in 0.1.0 - TL layer 233

- Upgraded the MTProto TL schema to layer 233 from the Kurigram reference and regenerated the C++ schema code.
  The client now sends layer 233. Offline tests cover the new constructor IDs and wire layout.
- Added `docs/KURIGRAM_PARITY.md` and a list of unused Kurigram TL functions. Feature parity is not complete. The list
  was later renamed `docs/KURIGRAM_UNUSED_TL_FUNCTIONS.txt`, and the parity document is now a pointer to `PARITY_MATRIX.md`.
  The file `docs/KURIGRAM_MISSING_METHODS.txt` named here does not exist.

### Included in 0.1.0 - coroutines, in-flight cancellation, transfer recovery, benchmarks

- C++20 coroutines (`cppgram/task.h`): `async::Task<T>` is lazy and move-only. `co_await` accepts `Task` and
  `this_task_stop_token()`. `async::spawn(runner, task)` runs it on a `Runner`. Exceptions and cancellation propagate.
- In-flight RPC cancellation: a wait for a reply ends within one 50 ms poll slice when the async operation is
  cancelled. A request cancelled before it is sent is never sent. A reply that arrives after a cancel is ignored.
  `PacketChannel::receive_packet_cancellable` is new; its default ignores the stop token.
- Synchronous `Client::stop()` is unchanged. It does not interrupt an RPC that a synchronous call waits for.
- Transfer recovery: a timeout or dropped connection during an upload or download part reconnects and resends the
  same part. A reconnect that never succeeds fails the transfer with a network error.
- Downloads: a second concurrent download to the same `.part` file in one process is refused with
  `FileTransferError`. Errors on a chunk are retried. Cancellation leaves the destination untouched.
- Benchmarks: `cppgram_runtime_bench` (`ENABLE_BENCHMARKS=ON`) measures metrics, async and media transfers on a
  loopback fake DC. Results and method: `docs/BENCHMARKS.md`.
- `errors::categorize` maps `mtproto::OperationCancelled` to `Category::Cancelled`.
- Documentation: `docs/ASYNC.md`, `docs/METRICS.md`, `docs/MEDIA.md`, `docs/BENCHMARKS.md`. The examples in them are
  mirrored and compiled by `tests/async/docs_examples_test.cpp`.

### Included in 0.1.0 - metrics, async APIs and media transfers (Phases 5-7)

- Metrics (`cppgram/metrics.h`): thread-safe counters, gauges and histograms in a `metrics::Registry`. Series are
  capped per family (default 128), with extra label sets folded into an `__overflow__` series. Label values pass
  through `log::redact`. Disabled registries and default-constructed handles are no-ops.
- Prometheus text exposition 0.0.4 (`cppgram/metrics_prometheus.h`): `render_prometheus()` and the content type
  constant. No HTTP server is included; expose the text with your own endpoint.
- `ClientOptions::metrics` (optional registry) records RPC counts and durations, connection attempts and
  connections, disconnects, reconnects, the connected gauge, transport errors by category, auth failures, flood
  waits, transferred bytes and part retries. `bot::Api::set_metrics` and `bot::CurlHttpClient::set_metrics` record
  Bot API requests, API errors, HTTP status codes and timeouts. The Bot API token never appears in metric labels.
- Asynchronous APIs (`cppgram/async.h`): `async::AsyncResult<T>` (move-only, `get`, `wait`, `cancel`, `link`) and
  `async::Runner`, a serial FIFO executor with one worker thread that starts on the first post. Destroying a runner
  cancels queued work and joins the worker. No detached threads and no global pool.
- `Client::async`, `connect_async`, `disconnect_async`, `login_bot_async` and `save_file_async` wrap the synchronous
  methods. Connect backoff and FLOOD_WAIT sleeps end promptly on cancel. An RPC already on the wire still ends at
  its receive timeout. The synchronous API is unchanged. `Client` is still single-threaded.
- Media transfers: uploads hash the file in one pass (MD5 via EVP) and send big-file parts with
  `upload_part_size` (0 selects the default). Parts retry on 5xx, network and timeout errors with backoff.
  `UploadProgress` reports bytes sent. `save_file` also accepts a progress callback and checks cancellation between
  parts.
- Downloads resume from a `.part` file. A `.part.meta` sidecar records the media fingerprint, and a mismatch is
  refused. A `FILE_REFERENCE_EXPIRED` answer is reported with a clear message. A size mismatch removes the partial
  files.
- ABI: `ClientOptions` gained trailing fields (`metrics`, `upload_part_size`). `Client`, `bot::Api` and
  `bot::CurlHttpClient` gained members and methods. `mtproto::MtSession` gained `set_flood_wait_observer`.

### Included in 0.1.0 - integration hardening (Phase 4.1)

- Structured logging is wired into the connect lifecycle, the login flow, RPC errors and flood waits, the transport
  (connection closed or recv failures), the Bot API (method name, error code, elapsed time) and the Curl transport.
  Logs carry method and status only, never the API hash, bot token, phone number, codes, passwords or session data.
- Curl transport errors are now `errors::NetworkError` with the method in the context. The request URL and the proxy
  URL are removed from error text, because the Bot API URL carries the token.
- Upload and download failures raised by the client are now `errors::FileTransferError`. Login state misuse is now
  `errors::AuthenticationError`. Both still derive from `std::runtime_error`, so existing catch blocks keep working.
- `mtproto::*` and `bot::TgException` are unchanged.
- WebApp: the strict duplicate-key rule and the minimal user-field rule are now documented in `webapp.h`.
- New tests in `tests/security/integration_test.cpp` (4 cases).

## Errors, logging and Web App initData

- New `cppgram/errors.h`: `cppgram::errors::Error` base (derives from `std::runtime_error`) with `Category`, `method()`, `code()`, `request_id()` and `retry_after()`. Types: NetworkError, TimeoutError, AuthenticationError, AuthorizationError, RpcError, BotApiError, FloodWaitError, SessionError, SerializationError, TransportError, FileTransferError, CancelledError.
- `errors::categorize(e)` and `errors::context_of(e)` classify every existing error (`mtproto::*`, `bot::TgException`) without changing those types.
- New `cppgram/log.h`: levels TRACE, DEBUG, INFO, WARN, ERROR, OFF; components; request ids; fields; pluggable sink; redaction of bot tokens and sensitive keys. Default level is WARN.
- New `cppgram/webapp.h`: Telegram Web App initData verification (HMAC-SHA256, `WebAppData` key derivation, constant-time compare, auth_date max age and future skew, duplicate and malformed checks). Parsed data is returned only after the signature verifies.
- Tests: `tests/security/` (49 cases). One known-answer vector was generated with Python `hmac`, independent of the C++ code.
- Not wired yet: the MTProto client and Bot API code do not call `log::write` yet.

## Source layout

- Renamed `src/client_*.cpp` to plain area names (`send.cpp`, `chats.cpp`, `stickers.cpp`, ...). `client.cpp` is unchanged. `client_batch5/6/7.cpp` became `misc_5/6/7.cpp`, and `client_bot.cpp` became `bot_methods.cpp` to avoid a clash with `Bot.cpp`. No code changed.
- README rewritten shorter.

## Build speed

- Makefile: uses Ninja when it is installed. `configure` runs once per build directory instead of on every `make build`.
- Makefile: `make live` builds only `cppgram_live_check` and `cppgram_live_tests` in `build/live`. `make live-offline-check` runs `tools/check_live_tests_offline.sh` against that build.
- Makefile: `make reconfigure` and `CTEST_PARALLEL_LEVEL` (default 1, so port-binding suites stay serial).
- CMake: `CPPGRAM_COMPILER_CACHE` (auto, ccache, sccache, off). sccache is now tried after ccache.
- CMake: `CPPGRAM_FAST_LINKER` (auto, mold, lld, off). The linker is test-linked before it is used.
- CMake: `CPPGRAM_UNITY_BUILD` (opt-in, OFF by default). Measured on a 2-core machine with tests on: cold build 8m10s without it and 6m31s with it, both without a compiler cache. 22 files stay outside the batches (the generated api_impl files, Types.cpp and files whose anonymous-namespace helpers clash). All 339 tests pass in both modes.
- Measured: with a warm ccache, a clean rebuild takes about 1.4 s instead of 8m20s.
- CMake: `CMAKE_EXPORT_COMPILE_COMMANDS` is on.
- Fixed: `.github/workflows/live.yml` read `secrets` in a job-level `if:`, which GitHub does not allow. The secrets check is now a step.

## Live tests

- `cppgram_live_tests` (BUILD_LIVE_TESTS=ON): six independent real-Telegram tests with PASS, FAIL, SKIP and
  NOT_SUPPORTED results. Reconnect uses a local relay to create a real network drop. session_restore checks encrypted
  session reuse and wrong-passphrase refusal.
- `tools/check_live_tests_offline.sh`: offline checks for SKIP, NOT_SUPPORTED and bad-secret paths.
- Ninja, optional ccache, and a Dockerfile that builds only the live binaries with a cached dependency layer.

### Included in 0.1.0

### Security and robustness (final offline round)
- Fixed: the encrypted session reader accepted scrypt cost up to N = 2^20 with r = 16, about 2 GiB of memory, from an untrusted file. The accepted range is now N up to 2^17 and r up to 8 (at most 128 MiB). The writer is unchanged.
- Fixed: encrypted session blobs above 16 MiB are refused before any work. `encrypt` enforces the same cap.
- Fixed: `SqliteStorage` leaked its SQLite handle when the constructor threw after `sqlite3_open`.
- Fixed: `SqliteStorage` read a NULL `sqlite3_column_text` result in the integrity check.
- Changed: `SqliteStorage` methods are serialized by a mutex. Two stores may share one file (WAL mode, busy timeout).
- Fixed: `Client::run()` set `running_` outside the lock that installs the stop source. A `stop()` in that window could be overwritten. The two are now set together.
- New: `Client::is_running()`.
- Changed: `decrypt_packet` ignores up to 15 trailing bytes after the ciphertext's 16-byte blocks (obfuscated padded-intermediate transport). Those bytes are not authenticated.
- New: CEG1 fuzz target `fuzz_session_crypto`. Invalid CEG1 seeds (`make_session_seeds`).
- Tests: session bound and size regressions, every-byte authentication, truncation at every length, storage concurrency (no torn reads, two stores on one file, corrupted and truncated files), client lifecycle (connect/disconnect cycles, concurrent stop, state reads during reconnects, no handler after destruction), padded server frames (many per read, split at every offset, truncated, server error word, padding overrun).
- Docs: `guides/SECURITY_AUDIT.md`, `guides/RECONNECT_AND_FLOOD_WAIT.md`, `guides/PROXIES.md`, Clang and GCC build instructions in `guides/LINUX_INSTALL.md`, fuzz README.

### Production readiness
- New: encrypted session fuzz target `fuzz_session_crypto` (CGE1 parsing and authenticated decryption). Added to `tools/run_fuzz_long.sh`.
- Changed: `decrypt_packet` ignores 0 to 15 trailing bytes after the ciphertext's 16-byte blocks. Obfuscated padded-intermediate frames may carry such padding. Trailing bytes are not authenticated, so this does not weaken msg_key checking. Tested: `Crypto.TrailingTransportPaddingIsIgnoredButExtraBlockIsRejected`, `ObfuscatedFrame.PaddedServerFramesDecryptToTheOriginalPacket`.
- Docs: `guides/RECONNECT_AND_FLOOD_WAIT.md`, `guides/PROXIES.md`.
- Fixed: `Client::is_connected()` is safe to call from any thread. It reads an atomic flag instead of the session pointer that `run()` replaces during a reconnect (ThreadSanitizer data race).
- Fixed: `~Client()` drops user handlers before disconnecting. A `Disconnected` event no longer runs user code after the handler's captured objects may be gone.
- Fixed: the reconnect budget resets after a successful reconnect. A failed initial connect no longer leaves `is_connected()` true.
- Fixed: `stop()` and `run()` no longer race on the stop source.
- Tests: client-level fake-DC tests for reconnect, FLOOD_WAIT, stop during backoff or wait, and concurrent stop and drop (`tests/mtproto/client_fake_dc_test.cpp`).
- CI: `.github/workflows/ci.yml` (header, schema and example checks; ASan and UBSan; TSan; clang-18 fuzz smoke run) and a manual `live.yml`. Not yet run on GitHub.
- Docs: `guides/PRODUCTION_READINESS.md` lists what is offline tested, CI verified, live tested, blocked and not verified.

### Login and authorization
- New: explicit authorization state machine (`cppgram/auth_state.h`, `Client::auth_state()`). Transition table and RPC error mapping are pure functions with tests.
- New: `Client::send_login_code`, `sign_in_code`, `sign_in_password`, `probe_session`; `LoginCodeInfo` result type.
- `RpcError::rpc_message()` returns the bare Telegram error name.
- `connect()` resets the state to `Unauthenticated`; `disconnect()` sets `Disconnected`; `run()` re-probes an authorized session after a reconnect.
- `login_bot` and `log_out` update the state. See `guides/AUTHENTICATION.md` (user login not live-tested).

### MTProto proxy and connection timeouts
- New: obfuscated MTProto proxy transport (`cppgram/mtproto/obfuscated.h`) and `ClientOptions::mtproto_proxy` (host, port, hex secret). Plain and padded-intermediate secrets are supported. Fake-TLS (`ee`) secrets are rejected. Offline tests only: live compatibility with Telegram proxy servers is not verified.
- Changed: direct TCP connects are bounded by `ClientOptions::connect_timeout_ms` (non-blocking connect with a deadline). Previously a connect could block for the kernel's full SYN timeout.
- Changed: send timeouts use the same value as receive timeouts.

### Networking and shutdown
- New: cancellable waits (`cppgram/mtproto/cancel.h`). Retry backoff, reconnect backoff and flood-wait sleeps stop when `Client::stop()` is called; `run()` returns cleanly.
- `Client::stop()` is safe to call from another thread. A new `run()` starts unstopped.

### Files and transfers
- New: upload planning (`cppgram/transfer.h`): part size, part count and offsets follow Telegram's rules; empty and oversized files are rejected before they are read.
- New: `sanitize_remote_filename` keeps only a safe local name (no paths, no dot names, no control characters).
- Fixed: `download_media` writes to `<path>.part`, checks the received size against the message, and renames on success. A failed download no longer leaves a partial file at the destination (finding F17).
- Changed: `read_file` reads in one call instead of byte by byte.

### Protocol (batch 11)
- Fixed (F16): a replayed or resent server message is acknowledged but not processed again. Bounded memory (4096 recent message ids).
- New: `ChannelPtsTracker` (per-channel pts bookkeeping). Not yet used by the update loop.

### CI and tooling
- Fixed: CI installs `libboost-dev` and `nlohmann-json3-dev` (both are required by `find_package`). Both jobs now.
- Fixed: the sanitizer job passes each CMake flag as one argument, with the same configuration as `tools/run_sanitizers.sh`. Both configure commands were run locally from scratch and succeed.
- New: `tools/check_headers.sh` compiles each of the 39 public headers alone. `*.inc.h` fragments are skipped.

### Examples and guides (batch 10)
- New examples: `send-text-history` (send, edit, history, delete), `media-transfer` (upload, download with progress), `graceful-shutdown` (SIGINT/SIGTERM and `Client::stop()`). Syntax-checked only.
- New guides: `VPS_OPERATIONS.md`, `VERSIONING.md`, `MIGRATION.md`, and an index at `guides/README.md`, linked from `README.md`.

### Examples and docs
- New: `examples/mtproto-login` (user login over MTProto, session restore, `run()` with stop). Syntax-checked only.
- New: `.env.example` (placeholders); `.gitignore` allows it.
- New guides: `guides/LINUX_INSTALL.md` (tested environment and package versions), `SESSIONS.md`, `ARCHITECTURE.md`, `TROUBLESHOOTING.md`.

### Fixes
- Generator: no unused `fl_flags` variable in generated code (F7). Regenerated output verified with `tools/check_schema.sh`.
- Test: handshake test uses the OpenSSL 3 EVP API instead of deprecated RSA calls (F8).
- Transport: documented that a malformed frame keeps throwing until the connection is replaced (F9), with a regression test.

### Tooling and measurements
- New: `bench/bench_main.cpp` and `tools/run_benchmarks.sh` (option `ENABLE_BENCHMARKS`). Results and the environment are in `guides/BENCHMARKS.md`.
- Documented: `guides/THREAT_MODEL.md`, `guides/API_SUPPORT.md`.

### API
- New: `archive_chats`, `add_recent_sticker`, `remove_recent_sticker`, `apply_gift_code`, `apply_boost`.
- New: `unpin_all_messages`, `set_contact_note`, `accept_terms_of_service`.
- New: `get_account_ttl`, `set_account_ttl`, `set_chat_ttl`, `set_chat_username`, `set_chat_discussion_group`, `toggle_join_to_send`, `toggle_forum_topics`, `set_chat_accent_color`, `set_chat_profile_accent_color`, `toggle_folder_tags`.

### Protocol and networking
- Server time-sync errors (bad_msg 16/17) correct the time offset and resend.
- Overall per-request deadline in `MtSession::invoke`.
- FLOOD_WAIT sleeps and resends within `RetryPolicy::max_flood_wait`; longer waits are returned.
- TCP connect retries with bounded exponential backoff and jitter (`ClientOptions::retry`).
- Session files are written atomically (temp, fsync, rename).
- `run()` reconnects after a dropped connection (bounded by `ClientOptions::max_reconnects`) and catches up on missed updates.
- Update queue is bounded by `ClientOptions::max_pending_updates`; `run()` drains it before reading more packets. Updates are never dropped.
- Update gating is a pure function (`cppgram/update_gate.h`): apply, duplicate or gap.

### Storage
- Session file format v2 (`CGS2`) with CRC-32; v1 files still load. Damaged files raise `SessionCorruptError`.
- SQLite session store: schema version stamp (`user_version = 2`), refuses newer schemas, `quick_check` on open, busy timeout, WAL journal.

### Tooling
- `tools/check_schema.sh` and `make tl-check`: regenerates the TL code and checks it matches the repository.

### Security
- DH parameters from the server are validated (2048-bit safe prime, generator residue, `1 < g_a < p-1`).
- `random_id` values come from OpenSSL `RAND_bytes` instead of `std::mt19937_64`.
- Session files and the sqlite session store are created with mode `0600`.
- msg_key verification uses a constant-time comparison.

### Tooling
- GitHub Actions: Release build and tests, plus an ASan/UBSan job.
- `tools/check_examples.sh` syntax-checks all examples.
- `tools/run_sanitizers.sh` runs the test suite under sanitizers locally.

### Fuzzing
- libFuzzer targets for the TL reader, session string decoder, and HTML/Markdown parser (`fuzz/`, `-DBUILD_FUZZERS=ON`).

### Docs
- `SECURITY.md`, `AUDIT.md`.
