# VPS live test guide (MTProxy and SOCKS5)

This guide is for a later live run. Nothing in this phase connected to Telegram or to a proxy. Do not treat any result
here as proof of live compatibility until a run on your own VPS produces the success indicators below.

## What you need

- A VPS you control, with outbound access to Telegram.
- A working build (`cmake --build <dir> --target cppgram_mtproto_test` must pass first).
- Your own Telegram API ID and hash. Read them from the environment. Never write them into a source file.
- For MTProxy: a proxy you run or rent, and its secret. For SOCKS5: a SOCKS5 server and credentials if it uses them.

## Environment variables the tests and examples read

These are the only names the repository reads. Set them in the shell. Do not paste values into logs or chat.

| Name | Used by |
|---|---|
| `TG_API_ID` | live test programs |
| `TG_API_HASH` | live test programs |
| `TG_TARGET` | live test programs (target of the test message, see the test source) |
| `TOKEN` | bot examples (bot token) |
| `CPPGRAM_SESSION_PASSPHRASE` | encrypted session storage |
| `CPPGRAM_TEST_DEBUG` | enables extra test output |

The repository has no environment variable for the MTProxy host, port or secret. Pass them to `ClientOptions` from your own
program. Keep the secret in a file readable only by your user (`chmod 600`), and read it at startup. Do not place it on a
command line, where `ps` can show it.

## Step 1: direct baseline

Run direct first. If direct fails, the problem is not the proxy.

Success indicators: the client reaches the authorised state, and a test message is accepted. If the direct run fails,
stop and fix the VPS network or the API credentials before testing a proxy.

## Step 2: MTProxy

Prepare the proxy configuration in your own program:

- `mtproto_proxy.host`, `mtproto_proxy.port`: your proxy.
- `mtproto_proxy.secret`: the secret as given by your proxy, in hex, Base64URL or standard Base64. A plain or `dd` secret
  is accepted in any of these. A fake-TLS (`ee`) secret parses but is refused by the transport, so it cannot connect.
- `mtproto_proxy.framing`: leave `Auto` first. The secret chooses padded intermediate for `dd`, and intermediate otherwise.
- `dc_id`: a DC the proxy can route. Use 2 for the production DC. Use 10002 only for a test environment.

Success indicators:

- No `ProxyError` is thrown. The client reaches the authorised state.
- A `getMe`-style call or a test message returns a result.

Failure indicators and what they mean:

| Message | Meaning | What to check |
|---|---|---|
| `MTProto proxy closed the connection before sending any data` | The proxy dropped the handshake | Secret, framing, DC id, and whether the proxy can reach Telegram |
| `MTProto proxy sent no data within N ms after the handshake` | The proxy accepted the TCP connection and stayed silent | The same list, plus the proxy's upstream route |
| `fake-TLS (ee) proxy secrets are not supported` | An `ee` secret was given | Use a plain or `dd` secret for this client |
| `padded proxy secret has the wrong length` | A `dd` secret is not 34 hex digits | Recheck the secret. Do not add or remove characters to make it fit |
| `connection refused` on the TCP connect | The proxy is not listening | The host, port and the proxy's firewall |

If `Auto` fails, try the framing override once. Use the framing your proxy's documentation names. Do not guess a framing
and keep changing it: each try is a new handshake with the proxy.

Log redaction: the `ProxyError` messages were tested not to contain the secret. The other log lines were not audited for it.
Before you share a log, search it for your secret and remove any line that contains it.

## Step 3: SOCKS5

Configure `ClientOptions::socks5` with the host, port and credentials if the server needs them. The tunnel carries plain
MTProto, so no secret is needed.

Success indicators: the same as Step 1.

Failure indicators:

| Message | Meaning |
|---|---|
| `SOCKS5 proxy sent a wrong version` | The port is not a SOCKS5 server, or it is some other proxy type |
| reply code 5 (connection refused) | The SOCKS5 server could not reach Telegram |
| reply code 2 (connection not allowed by the server's rules) | The server's ruleset denies this target |
| an authentication error from the SOCKS5 server | The user name or password is wrong, or the server does not offer that method |

## Step 4: separating a local proxy failure from an upstream failure

- A failure on the TCP connect to the proxy is a local or network problem. Fix the host, the port or the firewall.
- A `ProxyError` after the handshake points to the secret, the framing, the DC or the proxy's upstream route.
- A failure with direct Telegram working and the proxy failing is a proxy problem, not a client problem.

## Step 5: record the result

Write down: the proxy type, the framing chosen (`Auto` or the override), the DC id, the exact message, and the time. Do not
record the secret, the API hash or the session file.
