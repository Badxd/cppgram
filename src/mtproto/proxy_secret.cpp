#include "cppgram/mtproto/proxy_secret.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cppgram::mtproto {
namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decodes hex into bytes. Returns false on odd length or a non-hex character. Never includes the input in errors.
bool decode_hex(const std::string& text, std::string& out) {
    if (text.size() % 2 != 0) return false;
    out.clear();
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int hi = hex_value(text[i]);
        const int lo = hex_value(text[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

}  // namespace

ProxySecret parse_proxy_secret(const std::string& hex) {
    ProxySecret secret;
    std::string bytes;
    if (hex.size() == 32) {
        secret.kind = ProxySecretKind::Plain;
        if (!decode_hex(hex, bytes)) throw std::invalid_argument("proxy secret is not valid hex");
        std::copy(bytes.begin(), bytes.end(), secret.key.begin());
        return secret;
    }
    if (hex.size() < 34) throw std::invalid_argument("proxy secret has an unsupported length");

    const std::string tag = hex.substr(0, 2);
    const std::string body = hex.substr(2, 32);
    if (tag == "dd") {
        if (hex.size() != 34) throw std::invalid_argument("padded proxy secret has the wrong length");
        secret.kind = ProxySecretKind::PaddedIntermediate;
    } else if (tag == "ee") {
        if (hex.size() <= 34) throw std::invalid_argument("fake-TLS proxy secret must include a domain");
        secret.kind = ProxySecretKind::FakeTls;
    } else {
        throw std::invalid_argument("proxy secret has an unknown prefix");
    }
    if (!decode_hex(body, bytes)) throw std::invalid_argument("proxy secret is not valid hex");
    std::copy(bytes.begin(), bytes.end(), secret.key.begin());

    if (secret.kind == ProxySecretKind::FakeTls) {
        std::string domain_bytes;
        if (!decode_hex(hex.substr(34), domain_bytes)) {
            throw std::invalid_argument("fake-TLS domain is not valid hex");
        }
        if (domain_bytes.empty() || domain_bytes.size() > 253) {
            throw std::invalid_argument("fake-TLS domain must be 1..253 bytes");
        }
        for (char c : domain_bytes) {
            const auto u = static_cast<unsigned char>(c);
            if (u < 0x21 || u > 0x7e) throw std::invalid_argument("fake-TLS domain has non-printable characters");
        }
        secret.fake_tls_domain = std::move(domain_bytes);
    }
    return secret;
}

}  // namespace cppgram::mtproto
