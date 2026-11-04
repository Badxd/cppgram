# Security audit: scope, findings and status

Scope: a targeted review of the code paths that handle untrusted network data, session secrets, files, sockets,
threads and SQLite. About 55,000 lines of hand-written C++ under `src/` and `include/`, excluding the generated
schema files. This is a review, not a formal audit. Nothing here has been reviewed by a third party.

Status labels: IMPLEMENTED, OFFLINE TESTED, LOCALLY VERIFIED, NOT VERIFIED LIVE. See
[PRODUCTION_READINESS.md](PRODUCTION_READINESS.md).

## Findings fixed

| # | Area | Problem | Fix | Test |
|---|---|---|---|---|
| 1 | Encrypted session, resource use | A blob could ask for scrypt N = 2^20 with r = 16, about 2 GiB of memory, from an untrusted file | Accepted range is now N <= 2^17, r <= 8 (at most 128 MiB). Writer unchanged. | `SessionCrypto.ScryptCostAboveTheMemoryBoundIsRejectedBeforeDerivation` |
| 2 | Encrypted session, input size | No size cap; `size_t` was cast to `int` for OpenSSL calls | Blobs above 16 MiB are refused before any work. `encrypt` enforces the same cap. | `SessionCrypto.OversizedBlobIsRejectedBeforeAnyWork` |
| 3 | SQLite store, resource leak | A constructor that threw after `sqlite3_open` (for example on a failed `PRAGMA` or `prepare`) did not close the handle, because the destructor never runs | Body moved to `open_store()`; the constructor closes the handle on any throw | LeakSanitizer in the ASan run (`tools/run_sanitizers.sh`); `StorageConcurrency.CorruptedFileIsRejectedAndNotModified` exercises the failing open |
| 4 | SQLite store, undefined behavior | `sqlite3_column_text` can return NULL; the code built a `std::string` from it | NULL is checked before use | Covered by the corrupted-file tests |
| 5 | SQLite store, concurrency | One `sqlite3*` handle was used through methods that are not locked by the wrapper | Every public method takes a mutex | `StorageConcurrency.ConcurrentSaveAndLoadStateNeverTears`, `TwoStoresOnOneFileInterleaveWithoutErrors` |
| 6 | Connection state, data race | `is_connected()` read a pointer that `run()` replaces during reconnect | Reads an atomic flag set after the session is fully configured | TSan run; `ClientLifecycle.ConnectionStateIsReadableFromOtherThreadsDuringReconnects` |
| 7 | Handler lifetime | `~Client` ran user handlers while their captured objects could already be destroyed | Handlers are cleared before disconnect | `ClientLifecycle.HandlersDoNotRunAfterClientDestruction` |
| 8 | Padded transport | Valid server frames with 0 to 15 bytes of padding failed decryption | Trailing bytes beyond the 16-byte blocks are ignored; they are not authenticated, and an extra full block still fails msg_key checking | `Crypto.TrailingTransportPaddingIsIgnoredButExtraBlockIsRejected`, `ObfuscatedFrame.*` |

## Reviewed, no defect found

- Session file permissions: created with mode 0600 and forced on existing files (`src/secure_file.h`). Tested.
- Secret logging: debug output (`ClientOptions::debug`) prints connection stages, byte counts and file paths. It does not
  print keys, salts, passphrases, proxy secrets or SOCKS5 credentials. The SOCKS5 error messages do not include the
  username or password.
- TL decoding: every length read is checked against the remaining input (`TlReader`). Oversized `bytes` values are
  rejected when encoding.
- Transport framing: intermediate and abridged frames are capped at 16 MiB. Server error words in place of a length
  (for example -404) are reported, not treated as lengths.
- Reconnect budget and backoff: bounded, cancellable, reset only after a successful reconnect. Tested with the fake DC.
- SQL: every statement is either a constant string or a prepared statement. The one string-built statement sets
  `PRAGMA user_version` from an integer constant.
- Encrypted session format: AES-256-GCM with a fresh random nonce per encryption, the header as AAD, and the same error
  message for every authentication failure, so the failure mode does not reveal which check failed.

## Open items (not fixed)

- **Handler registration**: `Client::on_*` is not synchronized with event dispatch. Registering while `run()` is active is
  a data race. The header now documents that handlers must be registered before `connect()` or `run()`. Making
  registration thread-safe would change the event path and is not done in this round.
- **Padded server framing**: whether real Telegram servers pad their frames is NOT VERIFIED LIVE. The receiver accepts
  padding only inside encrypted packets, as described above.
- **Fake-TLS (`ee`) proxy secrets**: UNSUPPORTED. They are rejected before any connection is made.
- **Upload file reads**: an upload checks that each read returns the expected byte count and fails if the file changed.
  It does not lock the file. A concurrent writer can change content between parts without being detected beyond size.
- **Scrypt memory**: the 128 MiB bound applies to untrusted blobs only. The process still allocates that much while
  deriving a key, so a host with very little memory may need a lower bound in its own deployment.
