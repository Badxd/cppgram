# Live connectivity check

Runs a real login against Telegram from your own machine. Nothing is written except
`live-dcN.session` files (fresh per run) in the directory you run it from.

## Build

    cmake -S . -B build -DBUILD_LIVE_CHECK=ON -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target cppgram_live_check

## Run

    cd build
    TG_API_ID=<api_id> TG_API_HASH=<api_hash> TG_BOT_TOKEN=<bot token> ./cppgram_live_check

Optional:
- `TG_DCS=2,1,3,4,5` DC order (default). Migrate hints add DCs automatically.
- `TG_DEBUG=0` turns off stage logs.

The check does: TCP connect, auth key handshake, initConnection, `login_bot` (if a token is given), `get_me`.
Each stage that fails prints `FAIL stage=... DC=...` with the error. Send that output back when reporting a problem.

Credentials come only from environment variables. Revoke any token that was pasted in chat.
