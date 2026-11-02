# MTProto obfuscated proxy: plan (not implemented)

Status: design only. No code. The obfuscated transport is not implemented and is not verified against a real proxy.

## What exists now

- `mtproto::socks5_connect` returns a connected fd. `Connection::connect_via_socks5` uses it. `ClientOptions::socks5` routes the DC connection through it. Tested against a scripted loopback proxy only.
- The transport layer (`Transport`) sits on top of a connected fd. An obfuscated layer can wrap the fd without changing framing.

## What the obfuscated transport needs (to be verified against the official spec before coding)

Reference: https://core.telegram.org/mtproto/mtproto-transports (obfuscated section). Items below are my reading and must be checked:

1. Client sends a 64-byte random init. Derived keys come from SHA-256 over parts of the init and the proxy secret. Two AES-256-CTR streams: one for sending, one for receiving (the receive key uses the reversed init).
2. Bytes in the init carry the transport tag and DC id. They are encrypted with the send stream.
3. Secret formats to support:
   - 16-byte hex secret (plain)
   - `dd` prefix (padded intermediate transport)
   - `ee` prefix (fake-TLS). This needs a TLS ClientHello imitation and is a separate, larger item. Not in the first step.
4. Everything after the init goes through the CTR streams. Packet framing stays the existing intermediate transport.

## What I need from you to test

- Proxy host and port (the proxy you run or rent; do not paste a public list of secrets here)
- Secret type: plain 16-byte hex, `dd...`, or `ee...`
- Whether the test runs from the VPS (recommended) and which DC to connect to
- Test account credentials stay with you. Never paste them in chat.

## Order of work once the above is known

1. Implement plain-secret obfuscated init + CTR streams as a transport wrapper, behind the same fd interface.
2. Unit tests: key derivation against known vectors from the spec, round trip of init, wrong secret fails.
3. Live test on the VPS: connect via proxy to DC, do `help.getConfig`, record the result.
4. Only then `dd` and `ee`.

## Limits

- Until step 3 passes on a real proxy, the feature is not claimed as working.
- SOCKS5 remains the supported proxy path.
