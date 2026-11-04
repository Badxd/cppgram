# Proxies: SOCKS5 and MTProto obfuscated proxy

Status labels: see [PRODUCTION_READINESS.md](PRODUCTION_READINESS.md). Neither proxy has been tested against a live proxy
or against Telegram. Both are NOT VERIFIED LIVE.

Only one of the two can be set. `connect()` throws `std::invalid_argument` when both are set. Both apply to the main DC
connection only. CDN downloads connect directly.

## SOCKS5

```cpp
cppgram::ClientOptions o;
mtproto::Socks5Options s5;
s5.proxy_host = "127.0.0.1";
s5.proxy_port = 1080;
s5.auth = mtproto::Socks5Auth{/* username and password */};  // leave unset for no authentication
s5.timeout = std::chrono::milliseconds(10000);
o.socks5 = s5;
```

- Supported: CONNECT (RFC 1928), no-auth and username/password (RFC 1929).
- Destinations: IPv4 and IPv6 literals are sent as binary addresses. Any other host name is sent to the proxy as a domain
  name, so the proxy resolves it. The client does not resolve the DC address locally in that case.
- Timeouts: `timeout` applies to the connect and to every handshake read and write.
- Errors: `mtproto::Socks5Error` carries the RFC 1928 reply code when the proxy refused the request. Transient failures
  (timeouts, network errors) are retried according to `retry`. Rejected credentials and refused targets are not retried.
- Credentials are kept in memory only and are never logged.

Tested offline: protocol unit tests and a scripted loopback proxy cover method negotiation, credentials, IPv4, IPv6, domain
targets, refusal codes, timeouts and bounded retries.

## MTProto obfuscated proxy

```cpp
cppgram::ClientOptions o;
o.mtproto_proxy = cppgram::MtprotoProxyOptions{
    .host = "proxy.example.net",
    .port = 443,
    .secret = "<32 hex characters, or dd followed by 32 hex characters>",
    .connect_timeout_ms = 10000,
};
```

Secret formats:

- 32 hex characters: plain secret, intermediate framing.
- `dd` followed by 32 hex characters: padded-intermediate framing.
- `ee` followed by a domain (fake TLS): **rejected**. Not implemented.

The transport is AES-256-CTR. The key is SHA-256 of the handshake key material and the secret, and the handshake is 64
bytes. The handshake and framing follow the public obfuscated transport description and were cross-checked against two
independent open-source implementations. They were not checked against Telegram's own servers.

Known gap: the receive path reads frames with the intermediate reader. Padded server frames decrypt because the packet
decoder ignores up to 15 trailing bytes, which are not part of the authenticated ciphertext. Whether a real server pads
its frames is NOT VERIFIED.

The secret is never logged. Validation runs before any network I/O, so a bad secret fails without connecting.

Tested offline: obfuscated handshake, key and tag layout, CTR stream symmetry, intermediate and padded framing, fragmented
reads and writes, tampered and wrong-secret handshakes, a scripted proxy in plain and padded modes, proxy disconnect after
the handshake, and rejection of fake-TLS secrets.
