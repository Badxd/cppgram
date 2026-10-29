// cppgram - MTProto authorization key creation (req_pq_multi -> DH -> client_DH_params).
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/tl.h"

namespace cppgram::mtproto {

class AuthError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct RsaPublicKey {
    Bytes modulus;   // big-endian
    Bytes exponent;  // big-endian
};

Bytes hex_to_bytes(std::string_view hex);
// Production Telegram DC public keys.
const std::vector<RsaPublicKey>& telegram_public_keys();

// Message channel used by the handshake. Implementations handle framing; bodies are raw TL.
class PlainChannel {
public:
    virtual ~PlainChannel() = default;
    virtual void send_plain(const Bytes& body) = 0;
    virtual Bytes receive_plain() = 0;
};

// Unencrypted MTProto packet: auth_key_id(0) | message_id | length | body.
Bytes wrap_plain_message(const Bytes& body, std::uint64_t message_id);
Bytes unwrap_plain_message(const Bytes& packet);

// Message ids are time based (unix seconds << 32) and strictly increasing; low two bits are zero.
std::uint64_t make_message_id(std::int64_t time_offset = 0);

struct AuthKeyResult {
    Bytes auth_key;  // 256 bytes
    std::uint64_t server_salt = 0;
    std::int64_t time_offset = 0;  // server_time - local time, seconds
};

struct HandshakeOptions {
    std::int32_t dc_id = 2;
    std::vector<RsaPublicKey> keys;  // empty means telegram_public_keys()
};

AuthKeyResult create_auth_key(PlainChannel& channel, const HandshakeOptions& options = {});

// Validates server DH parameters: p must be a safe 2048-bit prime, g must satisfy its residue
// condition for p, and 1 < g_a < p - 1. Throws AuthError on any failure.
void validate_dh_params(const Bytes& prime_be, std::int32_t g, const Bytes& g_a_be);

}  // namespace cppgram::mtproto
