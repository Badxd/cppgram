#include "cppgram/mtproto/proxy_secret.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cppgram::mtproto {
namespace {

// Longest accepted text: 34 hex characters for "ee" + key, plus 2 hex characters per domain byte (253 at most).
constexpr std::size_t kMaxSecretText = 34 + 2 * 253;
constexpr std::size_t kMaxDomainBytes = 253;

// Decoded bytes. Each decoder reserves its full output size first, so the buffer never reallocates and leaves an
// unwiped copy behind. The buffer is overwritten when it is destroyed.
class DecodedBytes {
public:
    DecodedBytes() = default;
    ~DecodedBytes() {
        wipe();
    }
    DecodedBytes(const DecodedBytes&) = delete;
    DecodedBytes& operator=(const DecodedBytes&) = delete;

    std::vector<std::uint8_t>& bytes() {
        return bytes_;
    }
    const std::vector<std::uint8_t>& bytes() const {
        return bytes_;
    }

    void wipe() {
        volatile std::uint8_t* p = bytes_.data();
        for (std::size_t i = 0; i < bytes_.size(); ++i)
            p[i] = 0;
        bytes_.clear();
    }

private:
    std::vector<std::uint8_t> bytes_;
};

bool is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return c - 'A' + 10;
}

// True when every character is a hex digit (and the text is not empty).
bool is_hex_text(const std::string& text) {
    if (text.empty())
        return false;
    return std::all_of(text.begin(), text.end(), is_hex_digit);
}

bool decode_hex(const std::string& text, DecodedBytes& out) {
    out.wipe();
    if (text.size() % 2 != 0)
        return false;
    auto& b = out.bytes();
    b.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        if (!is_hex_digit(text[i]) || !is_hex_digit(text[i + 1])) {
            out.wipe();
            return false;
        }
        b.push_back(static_cast<std::uint8_t>((hex_value(text[i]) << 4) | hex_value(text[i + 1])));
    }
    return true;
}

// Value of one Base64 character in the chosen alphabet, or -1.
int base64_value(char c, bool url) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (url) {
        if (c == '-')
            return 62;
        if (c == '_')
            return 63;
    } else {
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
    }
    return -1;
}

// Strict Base64 decoder. Padding is optional. When present, '=' may appear only at the end (at most two) and the
// whole text must be a multiple of 4 characters. Unpadded text may not have a single leftover character (length mod
// 4 == 1). Trailing bits after the last full byte must be zero, which is what a canonical encoder writes.
bool decode_base64(const std::string& text, bool url, DecodedBytes& out) {
    out.wipe();
    std::size_t pad = 0;
    while (pad < text.size() && text[text.size() - 1 - pad] == '=')
        ++pad;
    if (pad > 2)
        return false;
    if (pad > 0 && text.size() % 4 != 0)
        return false;
    const std::size_t data = text.size() - pad;
    if (data % 4 == 1)
        return false;

    auto& b = out.bytes();
    b.reserve(data * 3 / 4 + 1);
    unsigned acc = 0;
    int bits = 0;
    for (std::size_t i = 0; i < data; ++i) {
        const int v = base64_value(text[i], url);
        if (v < 0) {
            out.wipe();
            return false;
        }
        acc = ((acc << 6) | static_cast<unsigned>(v)) & 0xFFFFu;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            b.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFFu));
        }
    }
    if (bits > 0 && (acc & ((1u << bits) - 1u)) != 0) {
        out.wipe();
        return false;
    }
    return true;
}

// Classifies decoded secret bytes. Messages state lengths and rules, never the bytes.
ProxySecret classify(const std::vector<std::uint8_t>& raw, ProxySecretEncoding encoding) {
    ProxySecret secret;
    secret.encoding = encoding;
    const std::size_t n = raw.size();

    if (n == 16) {
        secret.kind = ProxySecretKind::Plain;
        std::copy(raw.begin(), raw.end(), secret.key.begin());
        return secret;
    }
    if (n < 16) {
        throw std::invalid_argument("proxy secret decodes to " + std::to_string(n) + " bytes, expected at least 16");
    }
    if (raw[0] == 0xdd) {
        if (n != 17)
            throw std::invalid_argument("padded proxy secret has the wrong length");
        secret.kind = ProxySecretKind::PaddedIntermediate;
        std::copy(raw.begin() + 1, raw.end(), secret.key.begin());
        return secret;
    }
    if (raw[0] == 0xee) {
        if (n == 17)
            throw std::invalid_argument("fake-TLS proxy secret must include a domain");
        const std::size_t domain_len = n - 17;
        if (domain_len > kMaxDomainBytes)
            throw std::invalid_argument("fake-TLS domain must be 1..253 bytes");
        for (std::size_t i = 17; i < n; ++i) {
            if (raw[i] < 0x21 || raw[i] > 0x7e) {
                throw std::invalid_argument("fake-TLS domain has non-printable characters");
            }
        }
        secret.kind = ProxySecretKind::FakeTls;
        std::copy(raw.begin() + 1, raw.begin() + 17, secret.key.begin());
        secret.fake_tls_domain.assign(raw.begin() + 17, raw.end());
        return secret;
    }
    throw std::invalid_argument("proxy secret has an unknown prefix");
}

} // namespace

ProxySecret parse_proxy_secret(const std::string& text) {
    if (text.empty())
        throw std::invalid_argument("proxy secret is empty");
    if (text.size() > kMaxSecretText)
        throw std::invalid_argument("proxy secret is too long");

    DecodedBytes raw;
    ProxySecretEncoding encoding = ProxySecretEncoding::Hex;
    // Same order as TDLib: an even-length all-hex string is decoded as hex and nothing else is tried, so a Base64
    // secret that happens to be made only of hex digits is read as hex (and rejected if its length is wrong).
    if (is_hex_text(text) && text.size() % 2 == 0) {
        decode_hex(text, raw);
        encoding = ProxySecretEncoding::Hex;
    } else if (decode_base64(text, true, raw)) {
        encoding = ProxySecretEncoding::Base64Url;
    } else if (decode_base64(text, false, raw)) {
        encoding = ProxySecretEncoding::Base64;
    } else {
        throw std::invalid_argument(
            "proxy secret is not valid hex, Base64URL or Base64 (check for spaces, line breaks or copy errors)");
    }
    return classify(raw.bytes(), encoding);
}

} // namespace cppgram::mtproto
