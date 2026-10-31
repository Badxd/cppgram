# Threat model

This document lists what cppgram protects, against whom, and what it does not protect. It is a
maintainer's model, reviewed against the code in this repository. It is not a certification.

Using OpenSSL and AES-IGE does not make the library secure by itself. The protocol checks and the
handling of secrets on disk and in logs are what matter, and they are listed below with their status.

## Assets

| Asset | Where it lives | Impact if stolen |
|---|---|---|
| Authorization key (256 bytes) | Session file or SQLite store | Full access to the account until the session is terminated |
| Bot token | Environment variable or application config | Full control of the bot |
| API hash | Environment or config | Lets someone impersonate the application, not the account |
| 2FA password | Never stored by cppgram | Only the user knows it |
| Login code and phone code hash | Memory during login only | Can complete a login if used within its short lifetime |
| Message content and update state | Application handlers, optional SQLite store | Privacy of the user's messages |

A stolen authorized session is an account credential. Anyone holding the auth key can act as the user
and can read the account's messages, without the 2FA password. Revoke it from another Telegram client
(Settings, Devices) or call `terminate_session` from an authorized client.

## Trust boundaries

1. **Telegram servers and DCs.** Trusted for availability and for the RSA/DH parameters only after the
   checks in `validate_dh_params` (prime size, safe prime, generator residue, g_a range). Server messages
   are checked by msg_key (constant-time comparison) and by message-id and salt rules.
2. **The network between the client and Telegram.** Assumed hostile. MTProto encrypts and authenticates
   every message after the handshake. The transport framing (abridged, intermediate, full) is not
   authenticated and is validated only for length.
3. **The local file system.** Session files are created with mode 0600 and written atomically. A local
   user who can read the file can take the session, so the directory must be private to the service user.
4. **The application.** Handlers receive decrypted message content and run with the application's
   privileges. A handler that logs content leaks it.

## Attacker assumptions

| Attacker | Covered | Notes |
|---|---|---|
| Passive network observer | Yes | Traffic is encrypted; sizes and timing are visible |
| Active man in the middle on the TCP path | Partly | Wrong DH parameters are rejected; tampered packets fail the msg_key check. No certificate pinning is needed because MTProto does not use TLS |
| Replay of old encrypted packets | Partly | Not detected by the session layer: server message ids are not checked for duplicates (finding F16). Limited impact: rpc_result is matched to pending requests only, and replayed updates are dropped by the pts gate |
| Local user without root reading the session | No | Protect with file permissions and a private directory; encrypted storage is not implemented |
| Attacker with the session file | No | Equivalent to account access |
| Malicious server or DC | Partly | Malformed TL is rejected by bounded readers; no defence against Telegram itself |

## Attack surfaces and status

- **TL parser.** Vector counts and byte lengths are checked against the remaining buffer before
  allocation (`tl.h`, `tl.cpp`). Fuzz target `fuzz/fuzz_tl_reader.cpp` exists; the runs recorded in `AUDIT.md` are short.
- **Transport framing.** Negative length words and oversized frames are rejected. Decoder state after an
  error is documented in `AUDIT.md` (F9).
- **Session file.** CRC-32 detects accidental corruption, not tampering. An attacker who can write the
  file can also craft a valid CRC. Treat the file as a secret, not as a signed object.
- **SQLite store.** Refuses newer schema versions, runs `quick_check` on open, and uses WAL with
  `synchronous=FULL`. Still a plain file; see the encryption note above.
- **Logging.** The `debug_log` call sites print step descriptions, transport events, and error text. They
  do not print phone numbers, codes, hashes, passwords, bot tokens, or auth keys (checked by reading the
  call sites; no automated test enforces this yet). Review your logs before enabling debug output in production.
- **Denial of service.** Flood waits are honoured and bounded by `max_flood_wait`. Reconnects are bounded
  by `max_reconnects`. The update queue is bounded by `max_pending_updates` and never drops updates.
  Long-running handlers can still fill the queue; the client then stops reading packets until it drains.

## Random numbers

`random_id`, nonces, and padding use the OS CSPRNG through `mtproto::random_bytes`. The earlier
`std::mt19937_64` use for `random_id` was replaced (finding F5).

## Known limitations

- Session encryption at rest is not implemented. Use full-disk encryption or an encrypted volume.
- The library has not been reviewed by an external security auditor.
- Live behaviour against Telegram (handshake, reconnect, flood wait) is not verified in this repository.
- Benchmarks do not model an attacker; they measure cost only.

## Recommended deployment permissions

- Run the service as a dedicated unprivileged user.
- Session directory: owner-only (`chmod 700`), files `0600`, owned by the service user.
- Keep the bot token and API hash in an environment file readable only by the service user, outside the
  repository. `env.example` lists the names with placeholder values only.
- Do not run the client inside a shared container volume that other services can read.
- Back up the session file with the same permissions. A backup is as sensitive as the original.
