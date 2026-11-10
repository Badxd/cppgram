#include "cppgram/mtproto/obfuscated.h"

#include <openssl/evp.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "cppgram/mtproto/crypto.h"

namespace cppgram::mtproto {
namespace {

constexpr std::size_t kTagPos = 56;
constexpr std::size_t kDcPos = 60;
constexpr std::uint8_t kTagIntermediate[4] = {0xee, 0xee, 0xee, 0xee};
constexpr std::uint8_t kTagPadded[4] = {0xdd, 0xdd, 0xdd, 0xdd};
constexpr std::uint8_t kTagAbridged[4] = {0xef, 0xef, 0xef, 0xef};
constexpr std::size_t kMaxPadding = 15;

Bytes make_key(const Bytes& raw_key, const std::array<std::uint8_t, 16>& secret) {
    return sha256(concat(raw_key, Bytes(secret.begin(), secret.end())));
}

Bytes slice_range(const Bytes& data, std::size_t from, std::size_t to) {
    return Bytes(data.begin() + static_cast<std::ptrdiff_t>(from), data.begin() + static_cast<std::ptrdiff_t>(to));
}

// Derives the forward (client->proxy) and reverse (proxy->client) keys from a 64-byte handshake.
struct DirectionKeys {
    Bytes tx_key;
    Bytes tx_iv;
    Bytes rx_key;
    Bytes rx_iv;
};

DirectionKeys derive_keys(const Bytes& init, const std::array<std::uint8_t, 16>& secret) {
    DirectionKeys keys;
    keys.tx_key = make_key(slice_range(init, 8, 40), secret);
    keys.tx_iv = slice_range(init, 40, 56);
    Bytes reversed(init.rbegin(), init.rend());
    keys.rx_key = make_key(slice_range(reversed, 8, 40), secret);
    keys.rx_iv = slice_range(reversed, 40, 56);
    return keys;
}

bool has_prefix(const Bytes& data, const std::uint8_t* prefix, std::size_t n) {
    return std::memcmp(data.data(), prefix, n) == 0;
}

}  // namespace

struct CtrStream::Impl {
    EVP_CIPHER_CTX* ctx = nullptr;
    ~Impl() {
        if (ctx != nullptr) EVP_CIPHER_CTX_free(ctx);
    }
};

CtrStream::CtrStream(const Bytes& key, const Bytes& iv) : impl_(std::make_unique<Impl>()) {
    if (key.size() != 32 || iv.size() != 16) {
        throw std::invalid_argument("obfuscated transport needs a 32-byte key and a 16-byte iv");
    }
    impl_->ctx = EVP_CIPHER_CTX_new();
    if (impl_->ctx == nullptr) throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    if (EVP_CipherInit_ex(impl_->ctx, EVP_aes_256_ctr(), nullptr, key.data(), iv.data(), 1) != 1) {
        throw std::runtime_error("AES-256-CTR init failed");
    }
}

CtrStream::~CtrStream() = default;
CtrStream::CtrStream(CtrStream&&) noexcept = default;
CtrStream& CtrStream::operator=(CtrStream&&) noexcept = default;

Bytes CtrStream::apply(const Bytes& data) {
    Bytes out(data.size());
    if (data.empty()) return out;
    int len = 0;
    if (EVP_CipherUpdate(impl_->ctx, out.data(), &len, data.data(), static_cast<int>(data.size())) != 1 ||
        static_cast<std::size_t>(len) != data.size()) {
        throw std::runtime_error("AES-256-CTR update failed");
    }
    return out;
}

bool obfuscated_init_valid(const Bytes& init) {
    if (init.size() != kObfuscatedInitSize) return false;
    if (init[0] == 0xef) return false;
    static constexpr std::uint8_t kHead[4] = {'H', 'E', 'A', 'D'};
    static constexpr std::uint8_t kPost[4] = {'P', 'O', 'S', 'T'};
    static constexpr std::uint8_t kGet[4] = {'G', 'E', 'T', ' '};
    static constexpr std::uint8_t kTls[4] = {0x16, 0x03, 0x01, 0x02};
    if (has_prefix(init, kHead, 4) || has_prefix(init, kPost, 4) || has_prefix(init, kGet, 4)) return false;
    if (has_prefix(init, kTagIntermediate, 4) || has_prefix(init, kTagPadded, 4) || has_prefix(init, kTls, 4)) {
        return false;
    }
    if (init[4] == 0 && init[5] == 0 && init[6] == 0 && init[7] == 0) return false;
    return true;
}

ObfuscatedClient ObfuscatedClient::create(ObfuscatedFraming framing, std::int16_t dc_id,
                                          const std::array<std::uint8_t, 16>& secret,
                                          const std::optional<Bytes>& init_override) {
    Bytes init;
    if (init_override) {
        if (!obfuscated_init_valid(*init_override)) {
            throw std::invalid_argument("obfuscated init override is not a valid handshake");
        }
        init = *init_override;
    } else {
        for (;;) {
            init = random_bytes(kObfuscatedInitSize);
            if (obfuscated_init_valid(init)) break;
        }
    }

    const std::uint8_t* tag = kTagIntermediate;
    switch (framing) {
        case ObfuscatedFraming::Intermediate: tag = kTagIntermediate; break;
        case ObfuscatedFraming::PaddedIntermediate: tag = kTagPadded; break;
        case ObfuscatedFraming::Abridged: tag = kTagAbridged; break;
    }
    std::memcpy(init.data() + kTagPos, tag, 4);
    const auto dc_unsigned = static_cast<std::uint16_t>(dc_id);
    init[kDcPos] = static_cast<std::uint8_t>(dc_unsigned & 0xFF);
    init[kDcPos + 1] = static_cast<std::uint8_t>(dc_unsigned >> 8);

    DirectionKeys keys = derive_keys(init, secret);
    CtrStream tx(keys.tx_key, keys.tx_iv);
    CtrStream rx(keys.rx_key, keys.rx_iv);

    // The tail (tag and dc) is encrypted with the first 8 keystream bytes after the 56 raw bytes. Advancing
    // the TX stream over all 64 bytes leaves it positioned at the first data byte.
    const Bytes keystream = tx.apply(Bytes(kObfuscatedInitSize, 0));
    Bytes handshake = init;
    for (std::size_t i = kTagPos; i < kObfuscatedInitSize; ++i) {
        handshake[i] = static_cast<std::uint8_t>(init[i] ^ keystream[i]);
    }
    return ObfuscatedClient(framing, std::move(handshake), std::move(tx), std::move(rx));
}

Bytes ObfuscatedClient::frame(const Bytes& payload) const {
    Bytes out;
    if (framing_ == ObfuscatedFraming::Abridged) {
        if (payload.size() % 4 != 0) throw std::invalid_argument("abridged payload must be a multiple of 4 bytes");
        const std::size_t words = payload.size() / 4;
        if (words < 0x7f) {
            out.push_back(static_cast<std::uint8_t>(words));
        } else if (words < (1u << 24)) {
            out.push_back(0x7f);
            out.push_back(static_cast<std::uint8_t>(words));
            out.push_back(static_cast<std::uint8_t>(words >> 8));
            out.push_back(static_cast<std::uint8_t>(words >> 16));
        } else {
            throw std::invalid_argument("abridged payload too large");
        }
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }
    if (framing_ == ObfuscatedFraming::Intermediate) {
        out.reserve(4 + payload.size());
        const auto len = static_cast<std::uint32_t>(payload.size());
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(len >> (8 * i)));
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }
    const std::size_t pad = random_bytes(1)[0] % (kMaxPadding + 1);
    const Bytes padding = random_bytes(pad);
    const auto len = static_cast<std::uint32_t>(payload.size() + pad);
    out.reserve(4 + payload.size() + pad);
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(len >> (8 * i)));
    out.insert(out.end(), payload.begin(), payload.end());
    out.insert(out.end(), padding.begin(), padding.end());
    return out;
}

Bytes ObfuscatedClient::encrypt(const Bytes& data) {
    return tx_.apply(data);
}

Bytes ObfuscatedClient::decrypt(const Bytes& data) {
    return rx_.apply(data);
}

ObfuscatedServerView parse_obfuscated_handshake(const Bytes& handshake, const std::array<std::uint8_t, 16>& secret) {
    if (handshake.size() != kObfuscatedInitSize) {
        throw std::invalid_argument("obfuscated handshake must be 64 bytes");
    }
    DirectionKeys keys = derive_keys(handshake, secret);
    CtrStream from_client(keys.tx_key, keys.tx_iv);
    CtrStream to_client(keys.rx_key, keys.rx_iv);

    const Bytes keystream = from_client.apply(Bytes(kObfuscatedInitSize, 0));
    Bytes tail(kObfuscatedInitSize - kTagPos);
    for (std::size_t i = kTagPos; i < kObfuscatedInitSize; ++i) {
        tail[i - kTagPos] = static_cast<std::uint8_t>(handshake[i] ^ keystream[i]);
    }

    ObfuscatedFraming framing;
    if (std::equal(tail.begin(), tail.begin() + 4, kTagIntermediate)) {
        framing = ObfuscatedFraming::Intermediate;
    } else if (std::equal(tail.begin(), tail.begin() + 4, kTagPadded)) {
        framing = ObfuscatedFraming::PaddedIntermediate;
    } else if (std::equal(tail.begin(), tail.begin() + 4, kTagAbridged)) {
        framing = ObfuscatedFraming::Abridged;
    } else {
        throw std::invalid_argument("obfuscated handshake has an unknown protocol tag");
    }
    const auto dc_unsigned = static_cast<std::uint16_t>(tail[4] | (tail[5] << 8));
    return ObfuscatedServerView{framing, static_cast<std::int16_t>(dc_unsigned), std::move(from_client),
                                std::move(to_client)};
}

}  // namespace cppgram::mtproto
