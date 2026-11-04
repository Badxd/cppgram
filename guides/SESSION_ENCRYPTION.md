# Encrypted session files

Scope: file-based sessions (`session_path` that does not end in `.sqlite`, `.sqlite3` or `.db`).
Not covered yet: SQLite session storage and `export_session_string()`. Both hold the auth key in plain form.

## How it works

- Set `CPPGRAM_SESSION_PASSPHRASE` in the environment of the process. Nothing is read from a file.
- When it is set, every save writes a `CGE1` envelope: `magic | log2N | r | p | salt(16) | nonce(12) | tag(16) | ciphertext`.
- Key: scrypt (N=2^15, r=8, p=1) over the passphrase and a random 16-byte salt, giving a 32-byte key.
- Cipher: AES-256-GCM with a fresh random 12-byte nonce on every save. The header is authenticated as additional data.
- The KDF parameters are checked on load: log2N 14..17, r 1..8, p 1..4. Scrypt memory is 128 * N * r, so the largest accepted file needs 128 MiB. Values outside that range are refused before any memory is allocated.
- Blobs larger than 16 MiB are refused. Session files are a few kilobytes.

## Behaviour

| File on disk | Passphrase set | Result |
|---|---|---|
| `CGE1` | yes, correct | opens |
| `CGE1` | yes, wrong or tampered | `SessionCorruptError`, file is not changed |
| `CGE1` | no | `SessionCorruptError`, file is not changed |
| `CGS2`/`CGS1` (plain) | yes | opens, and is encrypted on the next save (migration) |
| `CGS2`/`CGS1` (plain) | no | opens as before |

Failures never fall back to a new login. A corrupt session is reported, not replaced.

## Passphrase requirements

- Use a long random passphrase (at least 20 characters from a password manager). scrypt slows guessing but does not make a weak passphrase safe.
- Do not put the passphrase in a file that sits next to the session. Keep it in a secret manager, or pass it when the service starts (systemd `EnvironmentFile` with mode 0600, owned by the service user).
- Do not pass the passphrase on the command line. Command lines show up in `ps` and in shell history.
- The library never logs the passphrase, the key, or the auth key.

## Backup and recovery

- Back up the session file and the passphrase separately. The file alone is unusable without the passphrase. The passphrase alone is useless without the file.
- A backup of an encrypted session is still a stolen-session risk if the passphrase is also stolen. Store both in different places.
- To recover: restore the file with mode 0600, set the same passphrase, start the client. If the passphrase is lost, the session cannot be decrypted. Log in again through a new session and delete the old one from Telegram's "Active sessions" list.
- Passphrase rotation is not implemented in this version. To change it, log in again with a new session file under the new passphrase, then delete the old session in Telegram's "Active sessions" list.

## Limits

- scrypt cost is fixed at N=2^15 for new files. Files written by a future version with other parameters still open, as long as they are within the accepted range (N up to 2^17, r up to 8). Earlier builds accepted up to 2^20 and r=16; those files are refused now. The writer in this code only ever produces N=2^15, r=8, p=1.
- There is no key escrow. A lost passphrase means a lost session.
- Encryption protects the file at rest. It does not protect a running process whose memory can be read by the same user.
