#include "cppgram/mtproto/auth.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>

#include "cppgram/mtproto/schema/api.h"

#include <openssl/bn.h>
#include <memory>


namespace cppgram::mtproto {

namespace {

struct BnDeleter {
    void operator()(BIGNUM* b) const { BN_free(b); }
};
using BnPtr = std::unique_ptr<BIGNUM, BnDeleter>;

}  // namespace

// Checks the server DH parameters the way the MTProto spec requires:
// p is a safe 2048-bit prime, g satisfies its residue condition, and 1 < g_a < p - 1.
void validate_dh_params(const Bytes& prime_be, std::int32_t g, const Bytes& g_a_be) {
    BN_CTX* raw_ctx = BN_CTX_new();
    if (raw_ctx == nullptr) throw AuthError("out of memory while checking DH parameters");
    std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)> ctx(raw_ctx, &BN_CTX_free);

    BnPtr p(BN_bin2bn(prime_be.data(), static_cast<int>(prime_be.size()), nullptr));
    BnPtr g_a(BN_bin2bn(g_a_be.data(), static_cast<int>(g_a_be.size()), nullptr));
    BnPtr p_minus_1(BN_new());
    BnPtr q(BN_new());
    if (!p || !g_a || !p_minus_1 || !q) throw AuthError("out of memory while checking DH parameters");

    if (BN_num_bits(p.get()) != 2048) throw AuthError("DH prime is not 2048 bits");
    if (BN_check_prime(p.get(), ctx.get(), nullptr) != 1) throw AuthError("DH prime is not prime");

    // Safe prime: q = (p - 1) / 2 must also be prime.
    if (BN_copy(p_minus_1.get(), p.get()) == nullptr || BN_sub_word(p_minus_1.get(), 1) != 1) {
        throw AuthError("DH parameter arithmetic failed");
    }
    if (BN_rshift1(q.get(), p_minus_1.get()) != 1 || BN_check_prime(q.get(), ctx.get(), nullptr) != 1) {
        throw AuthError("DH prime is not a safe prime");
    }

    // Generator residue conditions from the MTProto spec.
    const BN_ULONG p_mod_24 = BN_mod_word(p.get(), 24);
    const BN_ULONG p_mod_8 = p_mod_24 % 8;
    bool g_ok = false;
    switch (g) {
        case 2: g_ok = p_mod_8 == 7; break;
        case 3: g_ok = BN_mod_word(p.get(), 3) == 2; break;
        case 4: g_ok = true; break;
        case 5: { const BN_ULONG m = BN_mod_word(p.get(), 5); g_ok = m == 1 || m == 4; break; }
        case 6: g_ok = p_mod_24 == 19 || p_mod_24 == 23; break;
        case 7: { const BN_ULONG m = BN_mod_word(p.get(), 7); g_ok = m == 3 || m == 5 || m == 6; break; }
        default: g_ok = false;
    }
    if (!g_ok) throw AuthError("DH generator does not match the prime");

    // 1 < g_a < p - 1
    if (BN_cmp(g_a.get(), BN_value_one()) <= 0 || BN_cmp(g_a.get(), p_minus_1.get()) >= 0) {
        throw AuthError("g_a is out of range");
    }
}

namespace {

namespace fn = tl::functions;
namespace ty = tl::types;

std::uint8_t hex_nibble(char c) {
    if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
    throw AuthError("invalid hex digit");
}

Bytes u64_be_minimal(std::uint64_t value) {
    Bytes out;
    while (value != 0) {
        out.insert(out.begin(), static_cast<std::uint8_t>(value & 0xff));
        value >>= 8;
    }
    if (out.empty()) out.push_back(0);
    return out;
}

Bytes u32_be_minimal(std::uint32_t value) {
    return u64_be_minimal(value);
}

std::uint64_t le_u64(const Bytes& data) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(data[i]) << (8 * i);
    }
    return value;
}

// Key and iv for the server's DH answer (MTProto spec, "Server_DH_Params" section).
struct TmpAes {
    Bytes key;
    Bytes iv;
};

TmpAes tmp_aes(const Bytes& new_nonce, const Bytes& server_nonce) {
    const Bytes s1 = sha1(concat(new_nonce, server_nonce));
    const Bytes s2 = sha1(concat(server_nonce, new_nonce));
    const Bytes s3 = sha1(concat(new_nonce, new_nonce));
    TmpAes out;
    out.key = concat(s1, slice(s2, 0, 12));
    out.iv = concat(concat(slice(s2, 12, 8), s3), slice(new_nonce, 0, 4));
    return out;
}

// Parses a TL object that is prefixed with a hash: hash(20) | object | padding.
// Returns the object body reader position and verifies SHA1.
template <class T>
T read_hashed_object(const Bytes& decrypted, const char* what) {
    if (decrypted.size() < 20) {
        throw AuthError(std::string(what) + ": answer too short");
    }
    const Bytes payload(decrypted.begin() + 20, decrypted.end());
    TlReader reader(payload);
    const std::uint32_t id = reader.read_uint32();
    if (id != T::kId) {
        throw AuthError(std::string(what) + ": unexpected constructor id");
    }
    T value = T::read_body(reader);
    const std::size_t consumed = payload.size() - reader.remaining();
    if (sha1(slice(payload, 0, consumed)) != slice(decrypted, 0, 20)) {
        throw AuthError(std::string(what) + ": SHA1 mismatch");
    }
    return value;
}

Bytes build_hashed_object(const Bytes& object) {
    Bytes data = concat(sha1(object), object);
    const std::size_t rem = (16 - data.size() % 16) % 16;
    const Bytes pad = random_bytes(rem);
    return concat(data, pad);
}

}  // namespace

Bytes hex_to_bytes(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        throw AuthError("hex string has odd length");
    }
    Bytes out(hex.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::uint8_t>((hex_nibble(hex[2 * i]) << 4) | hex_nibble(hex[2 * i + 1]));
    }
    return out;
}

Bytes wrap_plain_message(const Bytes& body, std::uint64_t message_id) {
    TlWriter w;
    w.write_uint64(0);
    w.write_uint64(message_id);
    w.write_uint32(static_cast<std::uint32_t>(body.size()));
    w.write_raw(body);
    return w.take();
}

Bytes unwrap_plain_message(const Bytes& packet) {
    if (packet.size() < 20) {
        throw AuthError("plain packet too short");
    }
    TlReader r(packet);
    if (r.read_uint64() != 0) {
        throw AuthError("expected unencrypted packet (auth_key_id = 0)");
    }
    r.read_uint64();
    const std::uint32_t length = r.read_uint32();
    if (length != r.remaining()) {
        throw AuthError("plain packet length mismatch");
    }
    return r.read_raw(length);
}

std::uint64_t make_message_id(std::int64_t time_offset) {
    static std::atomic<std::uint64_t> counter{0};
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    const auto seconds = static_cast<std::uint64_t>(now + time_offset);
    const std::uint64_t seq = counter.fetch_add(1) & 0x3fffffu;
    return (seconds << 32) | (seq << 2);
}

AuthKeyResult create_auth_key(PlainChannel& channel, const HandshakeOptions& options) {
    const std::vector<RsaPublicKey> keys = options.keys.empty() ? telegram_public_keys() : options.keys;

    // Step 1: req_pq_multi -> resPQ
    const Bytes nonce = random_bytes(16);
    {
        fn::req_pq_multi req;
        req.nonce = nonce;
        TlWriter w;
        req.serialize(w);
        channel.send_plain(wrap_plain_message(w.bytes(), make_message_id()));
    }
    TlReader r1(unwrap_plain_message(channel.receive_plain()));
    const tl::ResPQ res_pq = tl::ResPQ::read(r1);
    const auto* pq_obj = std::get_if<ty::resPQ>(&res_pq.value);
    if (pq_obj == nullptr) {
        throw AuthError("expected resPQ");
    }
    if (pq_obj->nonce != nonce) {
        throw AuthError("resPQ nonce mismatch");
    }
    const Bytes server_nonce = pq_obj->server_nonce;
    if (pq_obj->pq.size() > 8) {
        throw AuthError("pq too large");
    }
    std::uint64_t pq_value = 0;
    for (const std::uint8_t b : pq_obj->pq) {
        pq_value = (pq_value << 8) | b;
    }
    const auto [p_val, q_val] = factorize_pq(pq_value);

    // Step 2: pick a key the server knows
    const RsaPublicKey* chosen = nullptr;
    std::uint64_t chosen_fp = 0;
    for (const RsaPublicKey& key : keys) {
        const std::uint64_t fp = rsa_fingerprint(key.modulus, key.exponent);
        for (const std::int64_t server_fp : pq_obj->server_public_key_fingerprints) {
            if (static_cast<std::uint64_t>(server_fp) == fp) {
                chosen = &key;
                chosen_fp = fp;
                break;
            }
        }
        if (chosen != nullptr) break;
    }
    if (chosen == nullptr) {
        throw AuthError("no known RSA key matches the server fingerprints");
    }

    // Step 3: req_DH_params with RSA-encrypted p_q_inner_data_dc
    const Bytes new_nonce = random_bytes(32);
    ty::p_q_inner_data_dc inner;
    inner.pq = u64_be_minimal(pq_value);
    inner.p = u64_be_minimal(p_val);
    inner.q = u64_be_minimal(q_val);
    inner.nonce = nonce;
    inner.server_nonce = server_nonce;
    inner.new_nonce = new_nonce;
    inner.dc = options.dc_id;
    TlWriter inner_w;
    inner.serialize(inner_w);
    const Bytes encrypted = rsa_encrypt_handshake(inner_w.bytes(), chosen->modulus, chosen->exponent);

    fn::req_DH_params dh_req;
    dh_req.nonce = nonce;
    dh_req.server_nonce = server_nonce;
    dh_req.p = u64_be_minimal(p_val);
    dh_req.q = u64_be_minimal(q_val);
    dh_req.public_key_fingerprint = static_cast<std::int64_t>(chosen_fp);
    dh_req.encrypted_data = encrypted;
    TlWriter dh_w;
    dh_req.serialize(dh_w);
    channel.send_plain(wrap_plain_message(dh_w.bytes(), make_message_id()));

    TlReader r2(unwrap_plain_message(channel.receive_plain()));
    const tl::Server_DH_Params server_params = fn::req_DH_params::read_result(r2);
    const auto* ok = std::get_if<ty::server_DH_params_ok>(&server_params.value);
    if (ok == nullptr) {
        throw AuthError("server returned server_DH_params_fail");
    }
    if (ok->nonce != nonce || ok->server_nonce != server_nonce) {
        throw AuthError("server_DH_params nonce mismatch");
    }

    // Step 4: decrypt server_DH_inner_data
    const TmpAes aes = tmp_aes(new_nonce, server_nonce);
    const Bytes answer = aes_ige_decrypt(ok->encrypted_answer, aes.key, aes.iv);
    const ty::server_DH_inner_data dh_inner = read_hashed_object<ty::server_DH_inner_data>(answer, "server_DH_inner_data");
    if (dh_inner.nonce != nonce || dh_inner.server_nonce != server_nonce) {
        throw AuthError("server_DH_inner_data nonce mismatch");
    }
    if (dh_inner.dh_prime.size() != 256) {
        throw AuthError("DH prime must be 2048 bits");
    }
    if (dh_inner.g < 2 || dh_inner.g > 7) {
        throw AuthError("invalid DH generator");
    }
    if (dh_inner.g_a.size() > 256) {
        throw AuthError("g_a too large");
    }
    validate_dh_params(dh_inner.dh_prime, dh_inner.g, dh_inner.g_a);

    // Step 5: our exponent, g_b and the auth key
    const Bytes b = random_bytes(256);
    const Bytes g_be = u32_be_minimal(static_cast<std::uint32_t>(dh_inner.g));
    const Bytes g_b = dh_power(g_be, b, dh_inner.dh_prime);
    const Bytes auth_key = dh_power(dh_inner.g_a, b, dh_inner.dh_prime);

    ty::client_DH_inner_data client_inner;
    client_inner.nonce = nonce;
    client_inner.server_nonce = server_nonce;
    client_inner.retry_id = 0;
    client_inner.g_b = g_b;
    TlWriter client_w;
    client_inner.serialize(client_w);
    const Bytes client_encrypted = aes_ige_encrypt(build_hashed_object(client_w.bytes()), aes.key, aes.iv);

    // Step 6: set_client_DH_params -> dh_gen_ok
    fn::set_client_DH_params set_req;
    set_req.nonce = nonce;
    set_req.server_nonce = server_nonce;
    set_req.encrypted_data = client_encrypted;
    TlWriter set_w;
    set_req.serialize(set_w);
    const std::int64_t offset_seconds = dh_inner.server_time - std::chrono::duration_cast<std::chrono::seconds>(
                                                                 std::chrono::system_clock::now().time_since_epoch())
                                                                 .count();
    channel.send_plain(wrap_plain_message(set_w.bytes(), make_message_id(offset_seconds)));

    TlReader r3(unwrap_plain_message(channel.receive_plain()));
    const tl::Set_client_DH_params_answer answer_obj = fn::set_client_DH_params::read_result(r3);
    const auto* gen_ok = std::get_if<ty::dh_gen_ok>(&answer_obj.value);
    if (gen_ok == nullptr) {
        throw AuthError("DH generation did not succeed");
    }
    if (gen_ok->nonce != nonce || gen_ok->server_nonce != server_nonce) {
        throw AuthError("dh_gen_ok nonce mismatch");
    }
    const Bytes aux = slice(sha1(auth_key), 0, 8);
    const Bytes expected_hash1 = slice(sha1(concat(concat(new_nonce, Bytes{0x01}), aux)), 4, 16);
    if (gen_ok->new_nonce_hash1 != expected_hash1) {
        throw AuthError("new_nonce_hash1 mismatch");
    }

    AuthKeyResult result;
    result.auth_key = auth_key;
    const Bytes salt_bytes = [&] {
        Bytes salt(8);
        for (std::size_t i = 0; i < 8; ++i) {
            salt[i] = new_nonce[i] ^ server_nonce[i];
        }
        return salt;
    }();
    result.server_salt = le_u64(salt_bytes);
    result.time_offset = dh_inner.server_time - std::chrono::duration_cast<std::chrono::seconds>(
                                                    std::chrono::system_clock::now().time_since_epoch())
                                                    .count();
    return result;
}

}  // namespace cppgram::mtproto
