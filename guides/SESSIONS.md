# Session management

A session is the authorization key and the server salt, time offset and DC for one account. The key is
a credential: anyone who has it can act as the account. Read `guides/THREAT_MODEL.md` first.

## Storage

Two formats are supported. Choose by the `session_path` extension.

| Path ends with | Format | Notes |
|---|---|---|
| anything else (for example `service.session`) | binary file, `CGS2` | Written atomically (temp file, fsync, rename). Mode 0600. CRC-32 over the body. Legacy `CGS1` files are read but never written. |
| `.sqlite`, `.sqlite3`, `.db` | SQLite database | Schema version in `PRAGMA user_version` (currently 2). WAL mode, `synchronous=FULL`, busy timeout 5 s, `quick_check` on open. A database with a newer schema version is refused, not overwritten. |

A damaged session file raises `SessionCorruptError` (a file with a bad checksum or unknown magic). It is
never silently replaced by a new login. Decide what to do with it yourself: restore a backup, or delete the
file and log in again.

## Lifecycle

1. `connect()` loads the session if one exists. If the key is missing, it creates one and saves it.
2. `probe_session()` confirms that the key is still authorized. See `guides/AUTHENTICATION.md`.
3. `run()` keeps the connection open and reconnects after a drop (bounded by `max_reconnects`).
4. `log_out()` invalidates the key on the server. Delete the session file afterwards.

Telegram can end a session at any time (a user terminated it, or the key was revoked). Then
`auth_state()` becomes `AuthRevoked`, and the session file is no longer useful.

## Session strings

`export_session_string()` returns a base64 form of the session. It contains the same secret as the file. Do
not print it in logs, put it in a ticket, or commit it. `fuzz/fuzz_session_string.cpp` checks the decoder for
crashes; it does not check that a string is trustworthy.

## Backup and restore

- Back up the file or the `.sqlite` database with the same permissions as the original. A backup is as
  sensitive as the session.
- For SQLite, use the `sqlite3` online backup command or `.backup` in the shell. Copying the `-wal` file
  while the process runs can give an inconsistent copy.
- To restore, stop the service, put the file back with mode 0600, start the service, and call
  `probe_session()`. If it returns `false`, log in again.

This procedure has not been tested against a live account in this repository.

## Encryption at rest

Not implemented. Use an encrypted volume or restrict the directory to the service user (`chmod 700`).
Encrypted session storage would need a key-management design (where the key lives, how it is rotated); that
design does not exist yet.
