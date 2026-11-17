# Running cppgram on a Linux VPS

The library is not a daemon. Your service runs it, so the points below are about your process. Nothing here
was tested against a live Telegram account from this repository.

## Users and directories

- Create a dedicated user: `useradd --system --home /var/lib/cppgram --shell /usr/sbin/nologin cppgram`.
- Session directory: `/var/lib/cppgram`, owned by `cppgram`, mode `0700`. Session files are `0600`, created by
  the library.
- Config (credentials): `/etc/cppgram/env`, owned by `root:cppgram`, mode `0640`. Copy the names from
  `.env.example`; never commit real values.
- Logs: `/var/log/cppgram/`, owned by `cppgram`.

## Environment variables

| Name | Used by | Notes |
|---|---|---|
| `TG_API_ID`, `TG_API_HASH` | your service, examples, `cppgram_live_check` | Secret pair. Not printed by the library |
| `TG_BOT_TOKEN` | bot logins and `cppgram_live_check` | Secret |
| `TG_DCS` | `cppgram_live_check` only | DC order, default `2,1,3,4,5` |
| `TG_DEBUG` | `cppgram_live_check` only | `0` turns off stage logs |

The library itself reads credentials from `ClientOptions`. It does not read the environment on its own.

## systemd unit (example)

```ini
[Unit]
Description=cppgram service
After=network-online.target
Wants=network-online.target

[Service]
User=cppgram
Group=cppgram
EnvironmentFile=/etc/cppgram/env
WorkingDirectory=/var/lib/cppgram
ExecStart=/opt/cppgram-app/bin/my-service
Restart=on-failure
RestartSec=10
# Stop on SIGTERM; the app should call client.stop() from its signal watcher (see examples/graceful-shutdown).
KillSignal=SIGTERM
TimeoutStopSec=30
# Hardening
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ReadWritePaths=/var/lib/cppgram /var/log/cppgram
ProtectHome=true

[Install]
WantedBy=multi-user.target
```

`Restart=on-failure` restarts the process after a crash. It does not fix an auth failure: a revoked
session (`AuthRevoked`) or a wrong login will fail again after every restart. Alert on it instead of
looping; the library gives up reconnecting after `max_reconnects`, so the process exits with an error.

## Signals and shutdown

- SIGTERM and SIGINT: set a flag in the handler; a watcher calls `Client::stop()`. `run()` returns after the
  current receive (at most `keepalive_ms` later). Then close the process.
- Do not call library methods from a signal handler.
- Keep `TimeoutStopSec` longer than `keepalive_ms` plus the longest handler you run.

## Restart and session recovery

- On start: `connect()`, then `probe_session()`. If it returns false, the session is revoked or expired; log
  it and stop (do not loop on login). Re-login needs a human for the code.
- Never delete the session file automatically after an error. Only a human decision should do that.
- A corrupt session raises `SessionCorruptError`. Keep the damaged file for inspection, restore from backup.

## Logs

- Log stage names, RPC names and error text. Do not log phone numbers, codes, passwords, bot tokens,
  session strings, or message bodies unless you have decided that you need them.
- `debug` output is verbose. Keep it off in production.
- Rotate with logrotate (`/etc/logrotate.d/cppgram`): `daily`, `rotate 14`, `compress`, `copytruncate`.

## Health and status

The library exposes `auth_state()` and `is_connected()`. A simple health check is: the process is alive and
`auth_state()` is `Authorized`. Add an HTTP endpoint or a file touch in your service; the library does not
include one.

## Disk space

- Session files are small (about 300 bytes). The SQLite store grows with update state, not with messages.
- Downloads go to `<name>.part` first. A crash can leave a `.part` file behind; delete stale ones by hand.
- Check free space before large uploads: the library reads the whole file into memory.

## Backups

- Back up the session file or the `.sqlite` file with the same permissions as the original.
- For SQLite, use `sqlite3 store.sqlite ".backup backup.sqlite"` while the service runs.
- Test the restore on a second machine before you need it.

## Limits to plan for

- The library has no built-in daemon, metrics, or health server.
- Memory: uploads load the whole file into RAM. Plan for the largest file you send.
- One account per process is the tested setup.
- Live behaviour (reconnect, flood wait, time sync) is not verified in this repository. Run a canary account
  for a few days before production.
