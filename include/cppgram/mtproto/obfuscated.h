// cppgram - MTProto obfuscated transport ("obfuscated2") used to talk to MTProto proxies.
//
// Client handshake (64 bytes, sent first, in the clear except the last 8 bytes which are encrypted):
//   protocol tag: 0xeeeeeeee (intermediate), 0xdddddddd (padded intermediate) or 0xefefefef (abridged, the 0xef
//   short tag padded to 4 bytes). The DC field: plain DC number, negated for media DCs, +10000 for test DCs.
//   bytes  0..56  random (rejected if they match a reserved prefix, see obfuscated_init_valid)
//   bytes 56..60  protocol tag: 0xeeeeeeee (intermediate) or 0xdddddddd (padded intermediate)
//   bytes 60..62  DC id, int16 little-endian
//   bytes 62..64  random
// Keys: TX key = SHA256(init[8..40] || secret), TX iv = init[40..56]. RX uses the reversed init the same way.
// The TX keystream is advanced over the 64-byte handshake, so the first encrypted data byte uses offset 64.
// The 16-byte secret is the proxy secret with any "dd" prefix removed. Fake-TLS ("ee") secrets are not supported.
//
// Implemented from the public MTProto transport description. Live compatibility with a real proxy is NOT
// verified by the offline tests in this repository.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "cppgram/mtproto/tl.h"

namespace cppgram::mtproto {

// Stateful AES-256-CTR keystream. One instance per direction. Move-only.
class CtrStream {
public:
    // key must be 32 bytes, iv 16 bytes. Throws std::invalid_argument otherwise.
    CtrStream(const Bytes& key, const Bytes& iv);
    ~CtrStream();
    CtrStream(CtrStream&&) noexcept;
    CtrStream& operator=(CtrStream&&) noexcept;
    CtrStream(const CtrStream&) = delete;
    CtrStream& operator=(const CtrStream&) = delete;

    // Encrypts or decrypts `data` (same operation for CTR) and advances the keystream.
    Bytes apply(const Bytes& data);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class ObfuscatedFraming {
    Intermediate,        // tag 0xeeeeeeee, frames: u32 length + payload
    PaddedIntermediate,  // tag 0xdddddddd, frames: u32 length (payload + padding) + payload + 0..15 random bytes
    Abridged,            // tag 0xefefefef, frames: u8 word count (or 0x7f + 3 bytes LE) + payload; payload % 4 == 0
};

constexpr std::size_t kObfuscatedInitSize = 64;

// True when a 64-byte candidate init avoids the reserved prefixes (0xef first byte, HEAD/POST/GET, the
// intermediate and padded tags, a TLS record header prefix, and four zero bytes at offset 4).
bool obfuscated_init_valid(const Bytes& init);

// Client side of the obfuscated transport. Produces the handshake to send first, then encrypts outgoing
// bytes and decrypts incoming bytes with the two directional keystreams.
class ObfuscatedClient {
public:
    // Creates a fresh handshake. `init_override` is for tests only: it must be 64 bytes, pass
    // obfuscated_init_valid, and is used instead of random bytes (tag and dc are still written into it).
    // Throws std::invalid_argument for a bad override.
    static ObfuscatedClient create(ObfuscatedFraming framing, std::int16_t dc_id,
                                   const std::array<std::uint8_t, 16>& secret,
                                   const std::optional<Bytes>& init_override = std::nullopt);

    ObfuscatedClient(ObfuscatedClient&&) noexcept = default;
    ObfuscatedClient& operator=(ObfuscatedClient&&) noexcept = default;

    // The 64 bytes to write to the socket before any frame.
    const Bytes& handshake() const noexcept { return handshake_; }
    ObfuscatedFraming framing() const noexcept { return framing_; }

    // Wraps one MTProto payload in the framing (padding is added for PaddedIntermediate), without encryption.
    Bytes frame(const Bytes& payload) const;
    // Encrypts outgoing bytes (handshake excluded; the handshake is already encrypted in its tail).
    Bytes encrypt(const Bytes& data);
    // Decrypts incoming bytes from the proxy.
    Bytes decrypt(const Bytes& data);

private:
    ObfuscatedClient(ObfuscatedFraming framing, Bytes handshake, CtrStream tx, CtrStream rx)
        : framing_(framing), handshake_(std::move(handshake)), tx_(std::move(tx)), rx_(std::move(rx)) {}

    ObfuscatedFraming framing_;
    Bytes handshake_;
    CtrStream tx_;
    CtrStream rx_;
};

// Server view of a client handshake. Used by tests to check that a handshake decodes to the values it encodes,
// and by anything that needs to play the proxy side offline. `to_client` encrypts for the client and `from_client`
// decrypts what the client sent.
struct ObfuscatedServerView {
    ObfuscatedFraming framing;
    std::int16_t dc_id;
    CtrStream from_client;
    CtrStream to_client;
};

// Throws std::invalid_argument when the handshake is not 64 bytes or its tag is unknown.
ObfuscatedServerView parse_obfuscated_handshake(const Bytes& handshake, const std::array<std::uint8_t, 16>& secret);

}  // namespace cppgram::mtproto
