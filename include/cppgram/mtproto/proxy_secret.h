// cppgram - MTProto proxy secret parsing (format only, no network or crypto).
// Layouts (hex text): 32 chars = 16-byte secret; "dd" + 32 chars = padded-intermediate marker + secret;
// "ee" + 32 chars + hex-encoded domain = fake-TLS marker + secret + SNI domain.
// These layouts come from third-party documentation and client sources. Check them against the official
// Telegram proxy documentation before relying on them. Parsing errors never include the secret text.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace cppgram::mtproto {

enum class ProxySecretKind { Plain, PaddedIntermediate, FakeTls };

struct ProxySecret {
    ProxySecretKind kind = ProxySecretKind::Plain;
    std::array<std::uint8_t, 16> key{};
    std::string fake_tls_domain;  // only for FakeTls, ASCII, 1..253 bytes
};

// Parses a secret given as hex text. Accepts upper or lower case. Throws std::invalid_argument on any
// malformed input (wrong length, non-hex, empty or invalid domain).
ProxySecret parse_proxy_secret(const std::string& hex);

}  // namespace cppgram::mtproto
