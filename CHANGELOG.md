# Changelog

## Unreleased

### Login and authorization
- New: explicit authorization state machine (`cppgram/auth_state.h`, `Client::auth_state()`). Transition table and RPC error mapping are pure functions with tests.
- New: `Client::send_login_code`, `sign_in_code`, `sign_in_password`, `probe_session`; `LoginCodeInfo` result type.
- `RpcError::rpc_message()` returns the bare Telegram error name.
- `connect()` resets the state to `Unauthenticated`; `disconnect()` sets `Disconnected`; `run()` re-probes an authorized session after a reconnect.
- `login_bot` and `log_out` update the state. See `guides/AUTHENTICATION.md` (user login not live-tested).

### Networking and shutdown
- New: cancellable waits (`cppgram/mtproto/cancel.h`). Retry backoff, reconnect backoff and flood-wait sleeps stop when `Client::stop()` is called; `run()` returns cleanly.
- `Client::stop()` is safe to call from another thread. A new `run()` starts unstopped.

### Files and transfers
- New: upload planning (`cppgram/transfer.h`): part size, part count and offsets follow Telegram's rules; empty and oversized files are rejected before they are read.
- New: `sanitize_remote_filename` keeps only a safe local name (no paths, no dot names, no control characters).
- Fixed: `download_media` writes to `<path>.part`, checks the received size against the message, and renames on success. A failed download no longer leaves a partial file at the destination (finding F17).
- Changed: `read_file` reads in one call instead of byte by byte.

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
