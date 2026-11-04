# Reconnect and FLOOD_WAIT

Status labels: see [PRODUCTION_READINESS.md](PRODUCTION_READINESS.md).

## Reconnect

`Client::run()` watches the connection. When it drops unexpectedly and `ClientOptions::auto_reconnect` is true (the default),
`run()` reconnects:

- Each attempt sleeps with exponential backoff and equal jitter: the delay is between half and all of the capped value.
  Defaults come from `ClientOptions::retry`: base 500 ms, cap 10 s.
- After `ClientOptions::max_reconnects` consecutive failed attempts (default 5), `run()` throws
  `mtproto::ConnectionError`. The budget resets after a successful reconnect.
- Calls that were in flight when the drop happened fail with `ConnectionError`. They are not resent automatically, because
  a resent request could run twice.
- After reconnecting, `run()` catches up on missed updates.
- `Client::stop()` cancels a backoff in progress and returns `run()` cleanly. A stopped client does not reconnect.
- `Client::is_connected()` can be called from any thread.
- `Client::on_connection_state` receives `Reconnecting`, `Connected` and `Disconnected` events.

A failed connect is torn down completely. `is_connected()` is false after a failed `connect()`.

Settings:

| Option | Default | Meaning |
|---|---|---|
| `auto_reconnect` | `true` | Reconnect when the connection drops |
| `max_reconnects` | `5` | Consecutive failed reconnect attempts before `run()` throws |
| `retry.max_attempts` | `3` | Attempts per TCP connect |
| `retry.base`, `retry.cap` | `500 ms`, `10 s` | Backoff range |
| `connect_timeout_ms` | `10000` | Connect and handshake timeout |

## FLOOD_WAIT

When Telegram answers `FLOOD_WAIT_n`, the client parses `n` as seconds.

- If `n` is at most `ClientOptions::retry.max_flood_wait` (default 60 s), the client sleeps and resends the same request.
  The sleep can be cancelled by `Client::stop()`.
- If `n` is larger than that limit, the client does not sleep. It throws `FloodWaitError` with the wait time, so the
  application can decide what to do.
- Resends are bounded by `retry.max_attempts`. A request that keeps getting FLOOD_WAIT stops after that many attempts.

FLOOD_WAIT handling is tested with a fake DC that returns controlled `FLOOD_WAIT` answers. No real rate limit was triggered.
Live behavior against Telegram is NOT VERIFIED LIVE.

## Testing

```
cmake --build build/Release --target cppgram_mtproto_test
build/Release/tests/cppgram_mtproto_test --gtest_filter='ClientFakeDc*'
```

`ClientFakeDc.*` covers unexpected drops, repeated drops, budget exhaustion, stop during backoff, stop preventing reconnect,
FLOOD_WAIT retry, FLOOD_WAIT beyond the limit, bounded FLOOD_WAIT attempts, stop during FLOOD_WAIT, and concurrent stop and
drop.
