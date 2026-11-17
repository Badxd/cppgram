# Architecture

## Layers

```
application handlers
        │
  cppgram::Client          include/cppgram/client.h, src/client*.cpp
  (high-level API, update loop, login state, DC routing)
        │
  mtproto::MtSession       src/mtproto/session.cpp     RPC correlation, deadlines, flood wait, salts
        │
  mtproto crypto/auth      src/mtproto/crypto.cpp, auth.cpp, srp.cpp, rsa_keys.cpp
        │
  mtproto::Connection      src/mtproto/connection.cpp  TCP socket, timeouts
        │
  mtproto::Transport       src/mtproto/transport.cpp   abridged / intermediate / full framing
```

Side modules:

| Module | Files | Role |
|---|---|---|
| TL codec | `src/mtproto/tl.cpp`, `tl.h` | Bounded reader/writer; vector counts checked against remaining bytes |
| Generated schema | `src/mtproto/schema/api_impl_*.cpp`, `include/.../schema/api.h` | Produced by `tools/tl_codegen`, reproducible (`tools/check_schema.sh`) |
| Retry policy | `mtproto/retry.*` | Backoff with jitter, error classes, reconnect budget |
| Cancellable waits | `mtproto/cancel.*` | Sleeps that stop when `Client::stop()` is called |
| Update gate | `update_gate.*` | Pure pts decision: apply, duplicate, gap |
| Auth state | `auth_state.*` | Pure transition table and RPC-error mapping |
| Transfer planning | `transfer.*` | Upload part sizes and offsets; file-name sanitising |
| Session storage | `session` in `mtproto/session.*`, `storage.*`, `secure_file.h` | Binary file and SQLite stores |
| Bot HTTP API | `Bot.cpp`, `TgLongPoll.cpp`, `TgWebhook*.cpp`, `Api*.cpp` | Separate Bot API client; not MTProto |

## Threads

The client is single-threaded. `run()` blocks on the socket and dispatches handlers on that thread.
`Client::stop()` may be called from another thread: it sets an atomic flag and requests a stop on a
`std::stop_source`, which interrupts retry and reconnect backoff. A blocked socket read finishes within
`keepalive_ms`. No background threads are started by the library.

## Request flow

1. A high-level method, or `Client::invoke` for a raw TL request, builds a TL function (`fn::...`).
2. `MtSession::invoke` assigns a message id, encrypts with the auth key, and sends through the transport.
3. The response is matched by `req_msg_id` (rpc_result). Other server messages (acks, salts, time
   corrections, updates) are handled in `dispatch`. Updates are queued.
4. Errors: `FLOOD_WAIT_n` sleeps and resends within `max_flood_wait`; bad salt or time-sync codes resend;
   other RPC errors throw `RpcError` with the code and the Telegram name.
5. The overall deadline (`request_timeout`) bounds the whole call, including unrelated packets.

## Update flow

Updates arrive in containers or short forms. Each carries pts/qts/seq/date. `classify_pts` decides whether
to apply, skip (duplicate) or fetch the difference (gap). Gaps trigger `updates.getDifference` through
`catch_up()`. The queue between the session and the handlers is bounded by `max_pending_updates`; when it is
full, `run()` drains it before reading more packets. Updates are not dropped.

## Ownership

- `Client` owns the connection, the session, the storage, and the update state.
- Handlers receive `const` references to library types; they must not keep references beyond the call.
- Public headers under `include/cppgram/` do not include OpenSSL headers (checked with grep). Generated TL
  types are reachable through `mtproto/schema/api.h`.

## Known architectural limits

- One account per `Client`.
- No per-channel pts tracking.
- Calls in flight are not resent after a reconnect; the caller must decide whether a retry is safe.
- Media upload reads the whole file into memory.
- No secret-chat or call support.
