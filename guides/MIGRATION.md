# Migration guide

This lists what changes for code and data that already uses cppgram. The sections are in the order you are
most likely to need them. Every item names the version where the change appears in `CHANGELOG.md`.

## Session files

- **Format `CGS2` (current).** Written with a CRC-32 and atomic replace. The old `CGS1` format is still read.
  The first save after upgrading writes `CGS2`. Downgrading to a build that only reads `CGS1` is not
  supported.
- **Corrupt files now fail loudly.** A file with a bad checksum raises `SessionCorruptError`. Before, a damaged
  file could be treated as "no session". Handle the exception: restore a backup or log in again.

## SQLite session store

- Schema version 2 (`PRAGMA user_version = 2`). A store written by a newer cppgram is refused with
  `SessionCorruptError`. Upgrade the library, do not delete the file.
- Opened in WAL mode with `synchronous=FULL`. The `-wal` and `-shm` files exist while the process runs. Back up
  with `.backup`, not by copying the main file alone.

## Login

- `auth_state()` is new. `connect()` now sets `Unauthenticated`, not "logged in". Call `probe_session()` after
  `connect()` to learn whether the saved key is still authorized.
- New login methods: `send_login_code`, `sign_in_code`, `sign_in_password`, `probe_session`. Previously there was
  no phone-login path in `Client`; use these instead of raw requests.
- `RpcError::rpc_message()` returns the bare Telegram name. Match on it, not on the `what()` text, which may
  change.

## Downloads

- `download_media` writes to `<path>.part` and renames on success. Code that read the destination while a
  download was running will now see the file only when it is complete.
- A size mismatch raises an error. Before, a short download was returned as success.

## Uploads

- Empty files and files over the Telegram limit are rejected before they are read (`std::runtime_error`
  with the reason).
- The file is still read into memory. Streaming upload is not implemented.

## Shutdown

- `Client::stop()` may be called from another thread. It also interrupts retry and reconnect backoff, which
  `stop()` did not do before. `run()` returns normally after `stop()`; a cancelled backoff is not reported as an
  error.
- A new `run()` starts unstopped.

## Retries and reconnects

- `connect()` retries TCP connection failures with backoff (`ClientOptions::retry`). Calls that used to fail
  immediately now wait up to the policy's cap before failing.
- `run()` reconnects after a dropped connection (`auto_reconnect`, `max_reconnects`). Calls in flight at the drop
  fail with `ConnectionError` and are not resent. Decide yourself whether a retry is safe.

## Removed or changed internals

- The `std::mt19937_64` random source for `random_id` was replaced by the OS CSPRNG. No API change.
- `mtproto::Transport::next()` keeps throwing after a malformed frame. Replace the connection instead of
  continuing to read from it.

## Checklist after upgrading

1. Build with `-DENABLE_TESTS=ON` and run `ctest`.
2. Start the service against a copy of your session file and check `auth_state()`.
3. Check that corrupt-session handling in your code catches `SessionCorruptError`.
4. Review any code that calls `download_media` or `stop()`.
