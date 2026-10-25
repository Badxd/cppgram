#include "cppgram/mtproto/crypto.h"

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace cppgram::mtproto {

namespace {

const std::array<std::uint32_t, 256>& crc_table() {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    return table;
}

Bytes digest(const EVP_MD* md, const Bytes& data) {
    Bytes out(EVP_MAX_MD_SIZE);
    unsigned int len = 0;
    if (EVP_Digest(data.data(), data.size(), out.data(), &len, md, nullptr) != 1) {
        throw CryptoError("EVP_Digest failed");
    }
    out.resize(len);
    return out;
}

class AesEcbBlock {
public:
    AesEcbBlock(const Bytes& key, bool encrypt) : ctx_(EVP_CIPHER_CTX_new()) {
        if (!ctx_ || EVP_CipherInit_ex(ctx_, EVP_aes_256_ecb(), nullptr, nullptr, nullptr, encrypt ? 1 : 0) != 1 ||
            EVP_CIPHER_CTX_set_key_length(ctx_, 32) != 1 ||
            EVP_CipherInit_ex(ctx_, nullptr, nullptr, key.data(), nullptr, encrypt ? 1 : 0) != 1 ||
            EVP_CIPHER_CTX_set_padding(ctx_, 0) != 1) {
            EVP_CIPHER_CTX_free(ctx_);
            ctx_ = nullptr;
            throw CryptoError("AES init failed");
        }
    }
    ~AesEcbBlock() { EVP_CIPHER_CTX_free(ctx_); }
    AesEcbBlock(const AesEcbBlock&) = delete;
    AesEcbBlock& operator=(const AesEcbBlock&) = delete;

    void run(const std::uint8_t* in, std::uint8_t* out) {
        int produced = 0;
        if (EVP_CipherUpdate(ctx_, out, &produced, in, 16) != 1 || produced != 16) {
            throw CryptoError("AES block failed");
        }
    }

private:
    EVP_CIPHER_CTX* ctx_;
};

void check_ige_args(const Bytes& data, const Bytes& key, const Bytes& iv) {
    if (key.size() != 32 || iv.size() != 32) {
        throw CryptoError("AES-IGE needs a 32-byte key and 32-byte iv");
    }
    if (data.size() % 16 != 0) {
        throw CryptoError("AES-IGE data must be a multiple of 16 bytes");
    }
}

struct AesParams {
    Bytes key;
    Bytes iv;
};

AesParams derive_aes_params(const Bytes& auth_key, const Bytes& msg_key, bool outgoing) {
    const std::size_t x = outgoing ? 0 : 8;
    const Bytes a = sha256(concat(msg_key, slice(auth_key, x, 36)));
    const Bytes b = sha256(concat(slice(auth_key, 40 + x, 36), msg_key));
    AesParams params;
    params.key = concat(concat(slice(a, 0, 8), slice(b, 8, 16)), slice(a, 24, 8));
    params.iv = concat(concat(slice(b, 0, 8), slice(a, 8, 16)), slice(b, 24, 8));
    return params;
}

Bytes compute_msg_key(const Bytes& auth_key, const Bytes& padded_plain, bool outgoing) {
    const std::size_t x = outgoing ? 0 : 8;
    return slice(sha256(concat(slice(auth_key, 88 + x, 32), padded_plain)), 8, 16);
}

Bytes pad_inner(const Bytes& inner) {
    const std::size_t rem = (16 - (inner.size() + 12) % 16) % 16;
    std::uint8_t extra_blocks = 0;
    RAND_bytes(&extra_blocks, 1);
    const std::size_t pad = 12 + rem + 16 * (extra_blocks % 4);
    Bytes out = inner;
    const Bytes padding = random_bytes(pad);
    out.insert(out.end(), padding.begin(), padding.end());
    return out;
}

Bytes bn_to_bytes(const BIGNUM* value, std::size_t size) {
    Bytes out(size, 0);
    const int needed = BN_num_bytes(value);
    if (static_cast<std::size_t>(needed) > size) {
        throw CryptoError("big number does not fit in requested size");
    }
    BN_bn2bin(value, out.data() + (size - static_cast<std::size_t>(needed)));
    return out;
}

std::uint64_t mulmod(std::uint64_t a, std::uint64_t b, std::uint64_t m) {
    return static_cast<std::uint64_t>((static_cast<unsigned __int128>(a) * b) % m);
}

std::uint64_t gcd_u64(std::uint64_t a, std::uint64_t b) {
    while (b != 0) {
        const std::uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

}  // namespace

std::uint32_t crc32(const Bytes& data, std::uint32_t previous) {
    std::uint32_t c = previous ^ 0xFFFFFFFFu;
    for (const std::uint8_t byte : data) {
        c = crc_table()[(c ^ byte) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

Bytes sha1(const Bytes& data) {
    return digest(EVP_sha1(), data);
}

Bytes sha256(const Bytes& data) {
    return digest(EVP_sha256(), data);
}

Bytes md5(const Bytes& data) {
    return digest(EVP_md5(), data);
}

Bytes random_bytes(std::size_t size) {
    Bytes out(size);
    if (size != 0 && RAND_bytes(out.data(), static_cast<int>(size)) != 1) {
        throw CryptoError("RAND_bytes failed");
    }
    return out;
}

Bytes slice(const Bytes& data, std::size_t offset, std::size_t size) {
    if (offset > data.size() || size > data.size() - offset) {
        throw std::out_of_range("slice out of range");
    }
    return Bytes(data.begin() + static_cast<std::ptrdiff_t>(offset),
                 data.begin() + static_cast<std::ptrdiff_t>(offset + size));
}

Bytes concat(const Bytes& a, const Bytes& b) {
    Bytes out;
    out.reserve(a.size() + b.size());
    out.insert(out.end(), a.begin(), a.end());
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

Bytes aes_ctr_crypt(const Bytes& data, const Bytes& key, const Bytes& iv) {
    if (key.size() != 32 || iv.size() != 16) {
        throw std::invalid_argument("AES-CTR needs a 32-byte key and a 16-byte iv");
    }
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    Bytes out(data.size());
    int len = 0;
    int total = 0;
    bool ok = EVP_CipherInit_ex(ctx, EVP_aes_256_ctr(), nullptr, key.data(), iv.data(), 1) == 1;
    if (ok && !data.empty()) {
        ok = EVP_CipherUpdate(ctx, out.data(), &len, data.data(), static_cast<int>(data.size())) == 1;
        total = len;
    }
    if (ok) ok = EVP_CipherFinal_ex(ctx, out.data() + total, &len) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) throw std::runtime_error("AES-CTR failed");
    return out;
}

Bytes cdn_counter_iv(const Bytes& base_iv, std::int64_t offset) {
    if (base_iv.size() != 16) throw std::invalid_argument("CDN iv must be 16 bytes");
    if (offset < 0 || offset % 16 != 0) throw std::invalid_argument("CDN offset must be a multiple of 16");
    Bytes iv = base_iv;
    const auto block = static_cast<std::uint32_t>(offset / 16);
    iv[12] = static_cast<std::uint8_t>(block >> 24);
    iv[13] = static_cast<std::uint8_t>(block >> 16);
    iv[14] = static_cast<std::uint8_t>(block >> 8);
    iv[15] = static_cast<std::uint8_t>(block);
    return iv;
}

Bytes aes_ige_encrypt(const Bytes& data, const Bytes& key, const Bytes& iv) {
    check_ige_args(data, key, iv);
    AesEcbBlock aes(key, true);
    Bytes prev_cipher(iv.begin(), iv.begin() + 16);
    Bytes prev_plain(iv.begin() + 16, iv.end());
    Bytes out(data.size());
    std::array<std::uint8_t, 16> x{};
    std::array<std::uint8_t, 16> y{};
    for (std::size_t off = 0; off < data.size(); off += 16) {
        for (std::size_t i = 0; i < 16; ++i) {
            x[i] = data[off + i] ^ prev_cipher[i];
        }
        aes.run(x.data(), y.data());
        for (std::size_t i = 0; i < 16; ++i) {
            y[i] ^= prev_plain[i];
            out[off + i] = y[i];
        }
        prev_cipher.assign(y.begin(), y.end());
        prev_plain.assign(data.begin() + static_cast<std::ptrdiff_t>(off),
                          data.begin() + static_cast<std::ptrdiff_t>(off + 16));
    }
    return out;
}

Bytes aes_ige_decrypt(const Bytes& data, const Bytes& key, const Bytes& iv) {
    check_ige_args(data, key, iv);
    AesEcbBlock aes(key, false);
    Bytes prev_cipher(iv.begin(), iv.begin() + 16);
    Bytes prev_plain(iv.begin() + 16, iv.end());
    Bytes out(data.size());
    std::array<std::uint8_t, 16> x{};
    std::array<std::uint8_t, 16> y{};
    for (std::size_t off = 0; off < data.size(); off += 16) {
        for (std::size_t i = 0; i < 16; ++i) {
            x[i] = data[off + i] ^ prev_plain[i];
        }
        aes.run(x.data(), y.data());
        for (std::size_t i = 0; i < 16; ++i) {
            y[i] ^= prev_cipher[i];
            out[off + i] = y[i];
        }
        prev_plain.assign(y.begin(), y.end());
        prev_cipher.assign(data.begin() + static_cast<std::ptrdiff_t>(off),
                           data.begin() + static_cast<std::ptrdiff_t>(off + 16));
    }
    return out;
}

Bytes serialize_inner(const InnerMessage& message) {
    TlWriter writer;
    writer.write_uint64(message.salt);
    writer.write_uint64(message.session_id);
    writer.write_uint64(message.message_id);
    writer.write_uint32(message.seq_no);
    writer.write_uint32(static_cast<std::uint32_t>(message.body.size()));
    writer.write_raw(message.body);
    return writer.take();
}

InnerMessage parse_inner(const Bytes& plain) {
    TlReader reader(plain);
    InnerMessage message;
    message.salt = reader.read_uint64();
    message.session_id = reader.read_uint64();
    message.message_id = reader.read_uint64();
    message.seq_no = reader.read_uint32();
    const std::uint32_t length = reader.read_uint32();
    if (length > reader.remaining()) {
        throw CryptoError("inner message length exceeds plaintext");
    }
    message.body = reader.read_raw(length);
    return message;
}

Bytes auth_key_id(const Bytes& auth_key) {
    return slice(sha1(auth_key), 12, 8);
}

Bytes encrypt_packet(const Bytes& auth_key, const Bytes& inner_plain, bool outgoing) {
    if (auth_key.size() != 256) {
        throw CryptoError("auth key must be 256 bytes");
    }
    const Bytes padded = pad_inner(inner_plain);
    const Bytes msg_key = compute_msg_key(auth_key, padded, outgoing);
    const AesParams params = derive_aes_params(auth_key, msg_key, outgoing);
    const Bytes cipher = aes_ige_encrypt(padded, params.key, params.iv);
    return concat(concat(auth_key_id(auth_key), msg_key), cipher);
}

Bytes decrypt_packet(const Bytes& auth_key, const Bytes& packet, bool outgoing) {
    if (auth_key.size() != 256) {
        throw CryptoError("auth key must be 256 bytes");
    }
    if (packet.size() < 24 || (packet.size() - 24) % 16 != 0) {
        throw CryptoError("encrypted packet has invalid size");
    }
    if (slice(packet, 0, 8) != auth_key_id(auth_key)) {
        throw CryptoError("auth_key_id mismatch");
    }
    const Bytes msg_key = slice(packet, 8, 16);
    const AesParams params = derive_aes_params(auth_key, msg_key, outgoing);
    const Bytes plain = aes_ige_decrypt(slice(packet, 24, packet.size() - 24), params.key, params.iv);
    if (compute_msg_key(auth_key, plain, outgoing) != msg_key) {
        throw CryptoError("msg_key mismatch");
    }
    return plain;
}

Bytes rsa_encrypt_handshake(const Bytes& data, const Bytes& modulus_be, const Bytes& exponent_be) {
    if (data.size() > 255 - 20) {
        throw CryptoError("RSA handshake payload too large");
    }
    Bytes block = concat(sha1(data), data);
    const Bytes padding = random_bytes(255 - block.size());
    block = concat(block, padding);

    BN_CTX* ctx = BN_CTX_new();
    BIGNUM* m = BN_bin2bn(block.data(), static_cast<int>(block.size()), nullptr);
    BIGNUM* n = BN_bin2bn(modulus_be.data(), static_cast<int>(modulus_be.size()), nullptr);
    BIGNUM* e = BN_bin2bn(exponent_be.data(), static_cast<int>(exponent_be.size()), nullptr);
    BIGNUM* c = BN_new();
    Bytes out;
    const bool ok = ctx && m && n && e && c && BN_mod_exp(c, m, e, n, ctx) == 1;
    if (ok) {
        out = bn_to_bytes(c, modulus_be.size());
    }
    BN_free(m);
    BN_free(n);
    BN_free(e);
    BN_free(c);
    BN_CTX_free(ctx);
    if (!ok) {
        throw CryptoError("RSA encryption failed");
    }
    return out;
}

std::uint64_t rsa_fingerprint(const Bytes& modulus_be, const Bytes& exponent_be) {
    TlWriter writer;
    writer.write_bytes(modulus_be);
    writer.write_bytes(exponent_be);
    const Bytes hash = sha1(writer.bytes());
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(hash[12 + i]) << (8 * i);
    }
    return value;
}

Bytes dh_power(const Bytes& base_be, const Bytes& exponent_be, const Bytes& modulus_be) {
    BN_CTX* ctx = BN_CTX_new();
    BIGNUM* base = BN_bin2bn(base_be.data(), static_cast<int>(base_be.size()), nullptr);
    BIGNUM* exp = BN_bin2bn(exponent_be.data(), static_cast<int>(exponent_be.size()), nullptr);
    BIGNUM* mod = BN_bin2bn(modulus_be.data(), static_cast<int>(modulus_be.size()), nullptr);
    BIGNUM* result = BN_new();
    Bytes out;
    const bool ok = ctx && base && exp && mod && result && BN_mod_exp(result, base, exp, mod, ctx) == 1;
    if (ok) {
        out = bn_to_bytes(result, modulus_be.size());
    }
    BN_free(base);
    BN_free(exp);
    BN_free(mod);
    BN_free(result);
    BN_CTX_free(ctx);
    if (!ok) {
        throw CryptoError("DH exponentiation failed");
    }
    return out;
}

std::pair<std::uint64_t, std::uint64_t> factorize_pq(std::uint64_t pq) {
    if (pq < 4) {
        throw CryptoError("pq too small to factor");
    }
    if (pq % 2 == 0) {
        return {2, pq / 2};
    }
    for (std::uint64_t c = 1; c < 64; ++c) {
        std::uint64_t x = 2;
        std::uint64_t y = 2;
        std::uint64_t d = 1;
        const auto f = [&](std::uint64_t v) { return (mulmod(v, v, pq) + c) % pq; };
        while (d == 1) {
            x = f(x);
            y = f(f(y));
            const std::uint64_t diff = x > y ? x - y : y - x;
            d = gcd_u64(diff, pq);
        }
        if (d != pq && d != 1) {
            const std::uint64_t other = pq / d;
            return d < other ? std::make_pair(d, other) : std::make_pair(other, d);
        }
    }
    throw CryptoError("failed to factor pq");
}

}  // namespace cppgram::mtproto
