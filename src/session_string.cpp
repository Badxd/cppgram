#include "cppgram/session_string.h"

#include <cstdint>

namespace cppgram {

namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int value_of(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::string base64_encode(const mtproto::Bytes& in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= in.size(); i += 3) {
        const std::uint32_t v = (std::uint32_t{in[i]} << 16) | (std::uint32_t{in[i + 1]} << 8) | in[i + 2];
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
    }
    const std::size_t rest = in.size() - i;
    if (rest == 1) {
        const std::uint32_t v = std::uint32_t{in[i]} << 16;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += "==";
    } else if (rest == 2) {
        const std::uint32_t v = (std::uint32_t{in[i]} << 16) | (std::uint32_t{in[i + 1]} << 8);
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

bool base64_decode(const std::string& text, mtproto::Bytes& out) {
    out.clear();
    std::uint32_t buf = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        const int v = value_of(c);
        if (v < 0) return false;
        buf = (buf << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return true;
}

// Layout: version(1)=1 | dc_id(4 LE) | time_offset(8 LE) | server_salt(8 LE) | auth_key(256).
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kSize = 1 + 4 + 8 + 8 + 256;

void put_le(mtproto::Bytes& out, std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}

std::uint64_t get_le(const mtproto::Bytes& in, std::size_t off, int n) {
    std::uint64_t v = 0;
    for (int i = 0; i < n; ++i) v |= std::uint64_t{in[off + i]} << (8 * i);
    return v;
}

}  // namespace

std::string encode_session_string(const mtproto::SessionData& data) {
    mtproto::Bytes raw;
    raw.reserve(kSize);
    raw.push_back(kVersion);
    put_le(raw, static_cast<std::uint32_t>(data.dc_id), 4);
    put_le(raw, static_cast<std::uint64_t>(data.time_offset), 8);
    put_le(raw, data.server_salt, 8);
    raw.insert(raw.end(), data.auth_key.begin(), data.auth_key.end());
    return base64_encode(raw);
}

bool decode_session_string(const std::string& text, mtproto::SessionData& out) {
    mtproto::Bytes raw;
    if (!base64_decode(text, raw) || raw.size() != kSize || raw[0] != kVersion) return false;
    out.dc_id = static_cast<std::int32_t>(static_cast<std::uint32_t>(get_le(raw, 1, 4)));
    out.time_offset = static_cast<std::int64_t>(get_le(raw, 5, 8));
    out.server_salt = get_le(raw, 13, 8);
    out.auth_key.assign(raw.begin() + 21, raw.end());
    return out.auth_key.size() == 256;
}

}  // namespace cppgram
