# Troubleshooting

Start with `TG_DEBUG=1` (or `ClientOptions::debug`) to see the stages. Copy only the stage name and the error
text into a report. Never paste a session file, an auth key, a bot token, a login code or a 2FA password.

| Symptom | Likely cause | What to do |
|---|---|---|
| `SessionCorruptError` on start | Session file or database damaged, or written by another program | Restore a backup, or delete the file and log in again |
| `session store schema version N is newer than this library supports` | The SQLite file was written by a newer cppgram | Use a matching or newer cppgram; do not delete the file |
| `AuthRevoked` after restart | The session was terminated in another client, or the key was revoked | Log in again; the old file is not usable |
| `PHONE_CODE_EXPIRED` | Code used after its lifetime | Call `send_login_code` again |
| `PHONE_CODE_INVALID` | Wrong code | Ask the user again; the state stays `CodeRequired` |
| `PASSWORD_HASH_INVALID` | Wrong 2FA password | Ask again; the state stays `PasswordRequired` |
| `FLOOD_WAIT_n` | Too many requests | Wait `n` seconds. Do not retry earlier. Reduce the request rate |
| `ConnectionError` after retries | Network, firewall, or DC address | Check outbound TCP to the DC on port 443 (or the configured port); try another DC in `TG_DCS` |
| `gave up reconnecting after N failed attempts` | Connection keeps dropping | Check the host's network. Raise `max_reconnects` only if the link is known to be flaky |
| `download size mismatch` | File changed on the server or a bad reference | Re-fetch the message and try again; the `.part` file was removed |
| `cannot open for writing: ...part` | Destination directory missing or not writable | Create the directory; check the service user's permissions |
| `upload rejected` | Empty file, file over 2000 MiB, or too many parts | Check the file; Telegram limits apply |
| Handler exceptions printed | An application handler threw | Install `on_error` to handle them |
| Build runs out of memory | Schema files compile large | Use `-j2` or lower, or add swap |
| `live_check` fails at `handshake` | Network path to Telegram is blocked from this host | Test from a host with direct access |

## Known gaps in the troubleshooting data

- Live behaviour of reconnect, flood wait and time sync is not verified in this repository. If a symptom is
  not in the table, report the stage output.
