# API support matrix

This file states what is actually verified. It does not list everything Telegram offers.

## Status levels

| Level | Meaning |
|---|---|
| Compiled | The declaration exists in `include/cppgram/client.h` and the implementation compiles in the Release build. |
| Offline-tested | A unit or integration test runs the code without Telegram (mock transport or pure function). |
| Live-verified | Someone ran the method against Telegram with a dedicated test account and recorded the result. |

**As of this revision:** about 240 `Client` methods are *Compiled*. Only the pure helpers and the protocol
layer have *Offline-tested* coverage (handshake, session, storage, retry, update gate, auth state machine,
transfer planning, transport framing). **No `Client` method has been live-verified in this repository.**
Treat every high-level method as unverified until your own test account confirms it.

The "phase N" labels in `client.h` are internal development batches, not Telegram API layers.
The supported Telegram layer is `kLayer` in `src/client.cpp` (currently 232). `tools/check_schema.sh`
checks that it matches `schema/main_api.tl`.

## Areas

| Area | Status | Notes |
|---|---|---|
| Connection, DC, DH handshake | Compiled, offline-tested (handshake, session tests); live pending | Retry on TCP connect, time sync, reconnect on drop |
| User login (send code, sign in, 2FA) | Compiled, offline-tested (state machine only); live pending | See `guides/AUTHENTICATION.md`. No sign-up, QR, or passkey |
| Bot login (`login_bot`) | Compiled; live pending | Token is a secret |
| Session restore (`probe_session`) | Compiled; live pending | |
| Logout, terminate session | Compiled; live pending | |
| Update loop, pts gate, getDifference | Compiled, pts gate offline-tested | Per-channel pts not implemented |
| Send, edit, delete, forward, copy messages | Compiled; live pending | |
| Chat history and pagination | Compiled; live pending | |
| Media upload (`save_file`, `send_uploaded`) | Compiled; live pending | Reads the whole file into memory. No progress callback. Not resumable |
| Media download (`download_media`) | Compiled; live pending | Progress callback. Not resumable. See finding F17 |
| Chats, members, admin rights | Compiled; live pending | Depends on account rights |
| Forum topics, stories, stars, business, checklists | Compiled; live pending | Feature availability depends on account and plan |
| Secret chats, voice/video calls, group calls | Not implemented | |
| Payments (invoices and receipts) | Partially compiled; not live-verified | Only the methods listed in `client.h` |
| Bot API (HTTP) classes (`Bot`, `TgLongPoll`, webhooks) | Separate code path; tested in `cppgram_bot_test` | Not MTProto |

## Bot API versus MTProto

A Bot account in cppgram uses MTProto with a bot token. Many Bot API methods are not available through
MTProto, and some MTProto user methods are refused for bot accounts. Do not assume a Bot API method has
an MTProto equivalent here.
