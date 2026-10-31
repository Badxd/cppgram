# Changelog

## Unreleased

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
