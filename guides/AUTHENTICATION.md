# Authentication guide

This describes the user-account login flow and the bot-token flow in cppgram. Read it together with
`SECURITY.md`: a logged-in session is an account credential.

## Status

- Bot token login (`Client::login_bot`): implemented, tested offline for state transitions, live-tested
  only by the developer on a test bot.
- User login (`send_login_code`, `sign_in_code`, `sign_in_password`, `probe_session`): implemented
  and covered by offline state-machine tests. **Not live-tested in this repository.** Run it on a
  dedicated test account before relying on it.
- QR login, passkeys, and sign-up of new accounts: not implemented. `sign_in_code` throws if the phone
  number has no Telegram account, because sign-up needs terms-of-service consent that cppgram does not
  collect.

## States

`Client::auth_state()` returns one of these values (`cppgram/auth_state.h`):

| State | Meaning | Typical next step |
|---|---|---|
| `Unauthenticated` | Connected, no authorization known for this session | `probe_session()` or `send_login_code()` |
| `Connecting` | Transport being established | wait for `connect()` to return |
| `Authenticating` | A login RPC is in flight | wait for the result |
| `CodeRequired` | Login code was sent | `sign_in_code()` |
| `PasswordRequired` | Account has 2FA | `sign_in_password()` |
| `Authorized` | Logged in | normal use |
| `Disconnected` | Closed by the application | `connect()` |
| `AuthExpired` | Server says the session expired | new login |
| `AuthRevoked` | Session ended elsewhere or key unregistered | new login |
| `RecoverableError` | Transient failure (network, flood wait) | `connect()` again |
| `PermanentError` | Input or account problem | fix the input, start over |

`disconnect()` is legal from every state, so shutting down during a login is always safe.

## User login

```cpp
cppgram::Client client(options);          // options.session_path set, api_id/api_hash from env
client.connect();
if (!client.probe_session()) {
    auto info = client.send_login_code(phone);       // phone in international format
    // info.delivery tells where the code went ("app", "sms", "call", ...)
    std::string code = read_code_from_user();
    if (!client.sign_in_code(phone, code, info.phone_code_hash)) {
        // 2FA account
        client.sign_in_password(read_password_from_user());
    }
}
// client.auth_state() == AuthState::Authorized
```

Error handling:

- Wrong code: `sign_in_code` throws `RpcError` with `rpc_message() == "PHONE_CODE_INVALID"`; the state
  stays `CodeRequired`, so the caller may ask again.
- Expired code: `PHONE_CODE_EXPIRED`; the state returns to `Unauthenticated`. Call `send_login_code` again.
- Wrong 2FA password: `PASSWORD_HASH_INVALID`; the state stays `PasswordRequired`.
- Flood wait: `FLOOD_WAIT_n`; the state becomes `RecoverableError`. Wait `n` seconds, then retry. Do not
  retry sooner, because repeated attempts extend the restriction.
- `PHONE_NUMBER_INVALID`, `USER_DEACTIVATED*`: `PermanentError`.

`send_login_code` and `sign_in_code` never print the phone number, code, hash, or password. Keep them out
of your own logs too.

## Session restored from disk

After `connect()` the state is `Unauthenticated` even when the session file holds a valid key. Call
`probe_session()` to find out. It returns `true` and moves to `Authorized` when the key works, and returns
`false` with `AuthRevoked` or `AuthExpired` when Telegram rejects it.

After a mid-session reconnect, `run()` probes the session again when it was authorized before the drop.

## Bot login

`login_bot(token)` imports a bot token with `auth.importBotAuthorization` and moves the state to
`Authorized`. The bot token is a secret: read it from the environment (for example `TG_BOT_TOKEN`), never
from source code, and never write it to a session file.

## Logout

`log_out()` calls `auth.logOut`, which invalidates the key on the server. Afterwards the session file
is useless, so delete it. Use `terminate_session(hash)` to end other sessions of the same account.

## Differences from the Bot API

Bot accounts can use only the subset of MTProto methods that Telegram allows for bots. Many Bot API
methods have no MTProto equivalent in cppgram, and some user-only methods are refused for bots.
Check `guides/API_SUPPORT.md` before assuming a method exists.
