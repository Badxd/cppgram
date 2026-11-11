// cppgram - MTProto proxy secret parsing (format only, no network or crypto).
//
// Text forms accepted, tried in this order (the order TDLib's ProxySecret::from_link uses):
//   1. Hexadecimal: any even-length string made only of hex digits. Nothing else is tried after it.
//   2. Base64URL (RFC 4648, section 5), then 3. standard Base64 (section 4). Padding is optional, but if present it
//      must be exact. Whitespace, other characters and non-canonical trailing bits are rejected.
// The decoded bytes are classified by length and prefix:
//   16 bytes                   plain secret
//   17 bytes, first 0xdd       padded-intermediate secret; the 0xdd byte is not part of the key
//   18 or more, first 0xee     fake-TLS secret; key = bytes 1..16, domain = the remaining 1..253 printable ASCII bytes
// The hex text of a fake-TLS secret is "ee" + 32 hex digits + the domain in hex, so hex and Base64 carry the same bytes.
// Fake-TLS is recognised here but not supported by the transport: Client and Connection refuse it.
// Nothing is truncated or padded. Errors never include the secret text or its bytes.
// Format rules: TDLib (td/mtproto/ProxySecret.cpp) and the MTProxy documentation. The Base64 rules were checked
// against TDLib's from_link order only. The TDLib base64 decoder itself is not in the local source copy.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace cppgram::mtproto {

enum class ProxySecretKind { Plain, PaddedIntermediate, FakeTls };

// Which text form the secret was given in. Never affects how the bytes are used.
enum class ProxySecretEncoding { Hex, Base64Url, Base64 };

struct ProxySecret {
    ProxySecretKind kind = ProxySecretKind::Plain;
    std::array<std::uint8_t, 16> key {};
    std::string fake_tls_domain; // only for FakeTls, ASCII, 1..253 bytes
    ProxySecretEncoding encoding = ProxySecretEncoding::Hex;
};

// Parses a secret given as text in one of the forms above. Throws std::invalid_argument on malformed input.
ProxySecret parse_proxy_secret(const std::string& text);

} // namespace cppgram::mtproto
