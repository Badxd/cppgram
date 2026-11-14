# MTProto proxy and transport audit

Scope: the client-to-proxy-to-Telegram path for the MTProto proxy (MTProxy, obfuscated transport) and the SOCKS5 path,
as implemented in this tree. This is an offline code and wire audit. No live proxy, Telegram server or credential was
used.

## Data path

| Stage | Route | Protocol at this stage | Code |
|---|---|---|---|
| Direct | app -> Telegram DC | plain TCP, MTProto transport chosen by `Connection` (unchanged this phase) | `Connection::connect` |
| MTProxy | app -> proxy -> Telegram DC | TCP, then 64-byte obfuscated handshake, then framed MTProto packets inside two AES-256-CTR streams. The proxy relays to the DC | `Connection::connect_obfuscated`, `ObfuscatedClient` |
| SOCKS5 | app -> SOCKS5 -> Telegram DC | TCP to the SOCKS5 server, greeting (no-auth or user/pass), CONNECT to `dc_ip:dc_port`, then plain MTProto transport through the tunnel. No obfuscation layer | `socks5_connect`, `Connection::connect_via_socks5` |

Client wiring (`src/client.cpp`, `connect_impl`):

1. `options.mtproto_proxy.secret` is parsed once, before any network activity (`parse_proxy_secret`). A malformed secret
   throws `std::invalid_argument`. A fake-TLS (`ee`) secret is refused the same way.
2. The framing defaults to the secret: a plain secret uses intermediate, a `dd` secret uses padded intermediate.
   `MtprotoProxyOptions::framing` can override it (`Auto`, `Intermediate`, `PaddedIntermediate`, `Abridged`).
3. A framing that does not fit the secret throws `std::invalid_argument` before the socket is opened
   (`Connection::connect_obfuscated`, 6-argument form).
4. Only transient `ProxyError`s are retried (`may_retry`). Rejected or final proxy errors are rethrown.

## Obfuscated handshake, checked against two independent implementations

Layout (from `include/cppgram/mtproto/obfuscated.h`, checked against `kat2/ref_mtproxy.py`):

- bytes 0..56 random, bytes 56..60 protocol tag, bytes 60..62 DC id as int16 little-endian, bytes 62..64 random.
- TX key = SHA256(init[8..40] || secret), TX iv = init[40..56]. RX uses the reversed init the same way.
- The TX keystream is advanced over all 64 bytes. Only bytes 56..64 are sent encrypted.

Two checks now cover this:

1. A known-answer test (`ProxyKnownAnswer.MatchesIndependentReferenceForEveryCase`) compares the handshake, an encrypted
   frame and a decrypted server reply, for intermediate (DC 2, -2 and 10002), padded and abridged, with bytes produced by
   `kat2/ref_mtproxy.py`. That script is a separate implementation written from the layout rules, not from cppgram code.
2. A loopback MTProxy stub (`StubProxy` in `tests/mtproto/proxy_transport_audit_test.cpp`) is written directly on
   OpenSSL and POSIX sockets. It derives its own keys, decodes the tag and DC from the handshake, and parses frames. The
   real `Connection` talks to it end to end.

Both agree with cppgram. No wire-level defect was found.

## Secret handling

- Accepted forms, tried in TDLib's order: hex, then Base64URL, then standard Base64. The decoded bytes decide the kind:
  16 bytes is plain, 17 bytes starting `0xdd` is padded intermediate, 18 or more starting `0xee` is fake-TLS (recognised,
  then refused by the transport). See the secret section of `FINAL_REPORT.md` for the verified rules.
- Upper and lower case hex are accepted. Whitespace is never trimmed. Anything else is refused.
- Errors never echo the input. A test checks this for malformed input and for a wrong secret at connect time.
- The secret is not logged. `ProxyError` messages name the check to make, not the value.
- Not implemented: wiping the key from memory after use. See `KNOWN_LIMITATIONS.md`.

## SOCKS5

Reviewed the greeting, method selection, user/pass sub-negotiation, CONNECT request for IPv4, IPv6 and domain, the reply
header, the bound-address reads, and every read or write via `socks5_connect`. Tests cover: fragmented method and reply
bytes (accepted), wrong version in the greeting, wrong version in the reply, unknown ATYP in the reply, and REP 5
(connection refused, reply code kept).

## Root cause of the reported MTProxy receive timeout: not identified

The earlier VPS symptom was "receive timed out" on the MTProxy path while direct Telegram worked. This audit could not
attribute that symptom to a client defect:

- The handshake bytes, the framing and the key derivation match two independent implementations.
- The client gave every silent-proxy case the same generic `ConnectionTimeout`, so the log could not separate causes.

What this phase changed is diagnostics, not a fix for the timeout:

- A proxy that sends nothing within the timeout after the handshake now raises `ProxyError` (transient) whose message
  names the secret, the `dd` prefix, the DC id and the proxy's own upstream connection. Before, it was a generic timeout.
- A proxy that closes before sending anything raises `ProxyError` (transient) with a message that names the same causes.

Causes that produce the same silent symptom, to check first in the live run (see `VPS_LIVE_TEST_GUIDE.md`):

1. Wrong secret. A proxy may drop the handshake without a reply, so the client waits for the timeout.
2. Framing mismatch. The proxy expects padded intermediate (`dd`) or abridged, and the client sent intermediate, or the
   reverse. The secret chooses the framing unless the `framing` option overrides it.
3. The proxy's own connection to Telegram failed. The client sees only silence, and the cause is on the proxy side.
4. DC id. The DC in the handshake must be one the proxy can route. Test DCs need +10000 (for example 10002).

Fake-TLS (`ee`) secrets are not the cause of this symptom. They fail before the network with `invalid_argument`. They are
still a common reason a link does not work, and they are unsupported.

## Changes in this phase

| File | Change |
|---|---|
| `include/cppgram/mtproto/obfuscated.h` | `ObfuscatedFraming::Abridged` added; header comment gives the tags and DC field |
| `src/mtproto/obfuscated.cpp` | Abridged tag `0xefefefef` and abridged framing (short form under 127 words, `0x7f` + 3-byte form above). Tag parsing recognises abridged |
| `include/cppgram/mtproto/connection.h`, `src/mtproto/connection.cpp` | 6-argument `connect_obfuscated` with explicit framing, validated before any network use. Silent-proxy and early-close diagnostics. Counter of encrypted bytes received since the handshake |
| `include/cppgram/client.h`, `src/client.cpp` | `MtprotoProxyOptions::Framing` (default `Auto`, which keeps the secret's framing) |
| `tests/mtproto/proxy_transport_audit_test.cpp` | New: known-answer vectors, abridged and padded framing, framing guard, secret parsing and redaction, loopback MTProxy stub (9 scenarios), loopback SOCKS5 stub (5 scenarios). 23 tests |
| `tests/CMakeLists.txt` | Registers the new file |
| `tools/format_allowlist.txt` | Adds the new file (it passes clang-format 18) |

The client-level `framing` option is wired, but no test drives it through `Client`. It is covered only by the code path and
by the connection-level tests. That is a stated limitation.

## Reference comparison

See the "Proxy and transport" section of `REFERENCE_COMPARISON.md`. The TDLib facts there come from reading the source in
`td-master`, not from running TDLib.

## 7. Resolution of F1/F2: plain-packet trailing bytes

Decision (approved): the plain-packet parser now follows TDLib. The 4-byte length field is authoritative, and any bytes
after the declared payload are ignored. Only a length field that claims more bytes than the packet holds is rejected
(`plain packet is truncated`). The 15-byte limit is removed.

Evidence: a live MTProxy in dd mode answered `req_pq_multi` and `req_DH_params` with 96, 108, 116 and 123 trailing bytes
after the length field, and one run passed the same stage with a padding count of 15 or less. The count varies per reply,
which fits random transport padding from the server rather than a client framing fault. Official clients accept these
replies because they ignore trailing bytes.

Unchanged: the transport's frame-length and maximum-frame-size checks (16 MiB), the truncation check, the auth_key_id = 0
check, and the minimum header size. The trailing part cannot grow an allocation, because the body copy is bounded by the
length field and the packet is already bounded by the transport.

Tests: `tests/mtproto/handshake_test.cpp` covers trailing counts 0 to 4096, the live counts 22, 96, 102, 108, 116 and 123,
truncation, a length field of 0xFFFFFFFF, a nonzero auth_key_id and a short header. The reference harness
(`tools/mtproxy_harness/frames_reference.py`) uses the same rule and regenerates its vectors.
