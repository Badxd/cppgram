// Obfuscated (MTProto proxy) transport: handshake layout, key derivation, framing and error handling.
// Offline only. The expected values come from the layout rules in obfuscated.h, not from captured proxy traffic,
// so these tests check internal consistency and the server-side decode path. They do NOT prove compatibility
// with the Telegram proxy servers.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/obfuscated.h"
#include "cppgram/mtproto/proxy_secret.h"
#include "cppgram/mtproto/transport.h"
#include <optional>

using namespace cppgram::mtproto;

namespace {

std::array<std::uint8_t, 16> secret_a() {
    std::array<std::uint8_t, 16> s{};
    for (std::size_t i = 0; i < s.size(); ++i) s[i] = static_cast<std::uint8_t>(0x11 * (i + 1));
    return s;
}

std::array<std::uint8_t, 16> secret_b() {
    auto s = secret_a();
    s[15] ^= 0x01;
    return s;
}

// A fixed, valid 64-byte init so the tests are deterministic.
Bytes fixed_init() {
    Bytes init(64);
    for (std::size_t i = 0; i < init.size(); ++i) init[i] = static_cast<std::uint8_t>(i * 7 + 3);
    init[0] = 0x42;  // not 0xef
    init[4] = 0x01;  // bytes 4..8 not all zero
    return init;
}

}  // namespace

TEST(ObfuscatedInit, RejectsReservedPrefixesAndZeroBytes) {
    Bytes ok = fixed_init();
    EXPECT_TRUE(obfuscated_init_valid(ok));

    Bytes bad = ok;
    bad[0] = 0xef;
    EXPECT_FALSE(obfuscated_init_valid(bad));

    const std::vector<std::vector<std::uint8_t>> prefixes = {
        {'H', 'E', 'A', 'D'}, {'P', 'O', 'S', 'T'}, {'G', 'E', 'T', ' '},
        {0xee, 0xee, 0xee, 0xee}, {0xdd, 0xdd, 0xdd, 0xdd}, {0x16, 0x03, 0x01, 0x02}};
    for (const auto& p : prefixes) {
        Bytes candidate = ok;
        std::copy(p.begin(), p.end(), candidate.begin());
        EXPECT_FALSE(obfuscated_init_valid(candidate));
    }

    Bytes zeros = ok;
    zeros[4] = zeros[5] = zeros[6] = zeros[7] = 0;
    EXPECT_FALSE(obfuscated_init_valid(zeros));

    EXPECT_FALSE(obfuscated_init_valid(Bytes(63, 0x42)));
}

TEST(ObfuscatedInit, OverrideMustBeValid) {
    Bytes bad = fixed_init();
    bad[0] = 0xef;
    EXPECT_THROW(ObfuscatedClient::create(ObfuscatedFraming::Intermediate, 2, secret_a(), bad),
                 std::invalid_argument);
}

TEST(ObfuscatedHandshake, ServerDecodesFramingAndDcForBothModes) {
    for (const std::int16_t dc : {std::int16_t{1}, std::int16_t{2}, std::int16_t{5}, std::int16_t{-1}}) {
        for (const auto framing : {ObfuscatedFraming::Intermediate, ObfuscatedFraming::PaddedIntermediate}) {
            auto client = ObfuscatedClient::create(framing, dc, secret_a(), fixed_init());
            ASSERT_EQ(client.handshake().size(), kObfuscatedInitSize);
            auto server = parse_obfuscated_handshake(client.handshake(), secret_a());
            EXPECT_EQ(server.framing, framing);
            EXPECT_EQ(server.dc_id, dc);
        }
    }
}

TEST(ObfuscatedHandshake, TagIsEncryptedOnTheWire) {
    // The plain tag 0xeeeeeeee must not appear in the clear at offset 56 of the handshake.
    auto client = ObfuscatedClient::create(ObfuscatedFraming::Intermediate, 2, secret_a(), fixed_init());
    const auto& h = client.handshake();
    EXPECT_FALSE(h[56] == 0xee && h[57] == 0xee && h[58] == 0xee && h[59] == 0xee);
    // Bytes before the tail are sent raw.
    const Bytes init = fixed_init();
    EXPECT_TRUE(std::equal(init.begin(), init.begin() + 56, h.begin()));
}

TEST(ObfuscatedHandshake, WrongSecretDoesNotDecode) {
    auto client = ObfuscatedClient::create(ObfuscatedFraming::Intermediate, 2, secret_a(), fixed_init());
    EXPECT_THROW(parse_obfuscated_handshake(client.handshake(), secret_b()), std::invalid_argument);
}

TEST(ObfuscatedHandshake, TamperedTailIsRejected) {
    auto client = ObfuscatedClient::create(ObfuscatedFraming::Intermediate, 2, secret_a(), fixed_init());
    Bytes tampered = client.handshake();
    tampered[57] ^= 0x80;  // changes the decoded tag
    EXPECT_THROW(parse_obfuscated_handshake(tampered, secret_a()), std::invalid_argument);
}

TEST(ObfuscatedHandshake, WrongSizeIsRejected) {
    EXPECT_THROW(parse_obfuscated_handshake(Bytes(63, 0), secret_a()), std::invalid_argument);
    EXPECT_THROW(parse_obfuscated_handshake(Bytes(65, 0), secret_a()), std::invalid_argument);
}

TEST(ObfuscatedStream, ClientToServerRoundTripAcrossSeveralWrites) {
    auto client = ObfuscatedClient::create(ObfuscatedFraming::Intermediate, 2, secret_a(), fixed_init());
    auto server = parse_obfuscated_handshake(client.handshake(), secret_a());

    const std::vector<Bytes> messages = {
        Bytes{1, 2, 3, 4}, Bytes(100, 0xAB), Bytes(17, 0x00), Bytes(4096, 0x5C), Bytes{9}};
    for (const auto& m : messages) {
        const Bytes wire = client.encrypt(m);
        EXPECT_EQ(wire.size(), m.size());
        EXPECT_EQ(server.from_client.apply(wire), m);
    }
}

TEST(ObfuscatedStream, ServerToClientRoundTrip) {
    auto client = ObfuscatedClient::create(ObfuscatedFraming::PaddedIntermediate, 5, secret_a(), fixed_init());
    auto server = parse_obfuscated_handshake(client.handshake(), secret_a());

    const Bytes reply(300, 0x3C);
    EXPECT_EQ(client.decrypt(server.to_client.apply(reply)), reply);
    const Bytes reply2{7, 7, 7};
    EXPECT_EQ(client.decrypt(server.to_client.apply(reply2)), reply2);
}

TEST(ObfuscatedStream, DirectionsUseDifferentKeystreams) {
    auto client = ObfuscatedClient::create(ObfuscatedFraming::Intermediate, 2, secret_a(), fixed_init());
    const Bytes zeros(32, 0);
    const Bytes out = client.encrypt(zeros);
    const Bytes in = client.decrypt(zeros);
    EXPECT_NE(out, in);
}

TEST(ObfuscatedStream, DataAfterHandshakeRoundTripsOnTheSharedStream) {
    // The TX stream is advanced over the handshake, so encrypting zeros must not reproduce the tail keystream.
    auto client = ObfuscatedClient::create(ObfuscatedFraming::Intermediate, 2, secret_a(), fixed_init());
    auto server = parse_obfuscated_handshake(client.handshake(), secret_a());
    const Bytes data(8, 0);
    const Bytes wire = client.encrypt(data);
    EXPECT_EQ(server.from_client.apply(wire), data);
}

TEST(ObfuscatedFrame, IntermediateFramingIsLengthPrefixed) {
    auto client = ObfuscatedClient::create(ObfuscatedFraming::Intermediate, 2, secret_a(), fixed_init());
    const Bytes payload{1, 2, 3, 4, 5, 6, 7, 8};
    const Bytes frame = client.frame(payload);
    ASSERT_EQ(frame.size(), 4 + payload.size());
    EXPECT_EQ(frame[0], 8);
    EXPECT_EQ(frame[1], 0);
    EXPECT_EQ(frame[2], 0);
    EXPECT_EQ(frame[3], 0);
    EXPECT_EQ(Bytes(frame.begin() + 4, frame.end()), payload);
}

TEST(ObfuscatedFrame, PaddedFramingAddsZeroToFifteenBytes) {
    auto client = ObfuscatedClient::create(ObfuscatedFraming::PaddedIntermediate, 2, secret_a(), fixed_init());
    const Bytes payload(20, 0x77);
    for (int i = 0; i < 64; ++i) {
        const Bytes frame = client.frame(payload);
        const std::uint32_t len = frame[0] | (frame[1] << 8) | (frame[2] << 16) | (static_cast<std::uint32_t>(frame[3]) << 24);
        ASSERT_GE(len, payload.size());
        ASSERT_LE(len, payload.size() + 15);
        ASSERT_EQ(frame.size(), 4 + len);
        EXPECT_EQ(Bytes(frame.begin() + 4, frame.begin() + 4 + payload.size()), payload);
    }
}

TEST(ObfuscatedCtr, RejectsBadKeyOrIvSizes) {
    EXPECT_THROW(CtrStream(Bytes(16, 0), Bytes(16, 0)), std::invalid_argument);
    EXPECT_THROW(CtrStream(Bytes(32, 0), Bytes(8, 0)), std::invalid_argument);
}

TEST(ObfuscatedCtr, EmptyInputGivesEmptyOutputAndIsSymmetric) {
    CtrStream a(Bytes(32, 1), Bytes(16, 2));
    EXPECT_TRUE(a.apply(Bytes{}).empty());
    CtrStream enc(Bytes(32, 1), Bytes(16, 2));
    CtrStream dec(Bytes(32, 1), Bytes(16, 2));
    const Bytes plain{'o', 'k'};
    EXPECT_EQ(dec.apply(enc.apply(plain)), plain);
}

TEST(ProxySecretForConnection, FakeTlsIsRejectedBeforeAnyNetworkUse) {
    // Parsing succeeds for an ee secret; the connection layer refuses it without touching the network.
    const std::string hex = "ee" + std::string(32, 'a') + "6578616d706c652e636f6d";  // domain hex
    const ProxySecret secret = parse_proxy_secret(hex);
    ASSERT_EQ(secret.kind, ProxySecretKind::FakeTls);
    Connection conn;
    EXPECT_THROW(conn.connect_obfuscated("127.0.0.1", 9, secret, 2, std::chrono::milliseconds(200)),
                 std::invalid_argument);
}

TEST(ObfuscatedStream, FragmentedReadsDecryptLikeOneRead) {
    // Bytes arrive in arbitrary fragments on a socket. The keystream must advance per byte, so decrypting
    // in pieces gives the same result as decrypting the whole stream at once.
    auto client = ObfuscatedClient::create(ObfuscatedFraming::Intermediate, 2, secret_a(), fixed_init());
    auto server = parse_obfuscated_handshake(client.handshake(), secret_a());
    const Bytes message(1000, 0x6D);
    const Bytes wire = server.to_client.apply(message);

    Bytes pieces;
    std::size_t pos = 0;
    const std::size_t steps[] = {1, 3, 7, 16, 64, 5};
    std::size_t step_index = 0;
    while (pos < wire.size()) {
        const std::size_t n = std::min(steps[step_index++ % 6], wire.size() - pos);
        const Bytes chunk(wire.begin() + static_cast<std::ptrdiff_t>(pos), wire.begin() + static_cast<std::ptrdiff_t>(pos + n));
        const Bytes out = client.decrypt(chunk);
        pieces.insert(pieces.end(), out.begin(), out.end());
        pos += n;
    }
    EXPECT_EQ(pieces, message);
}

TEST(ObfuscatedFrame, FragmentedFrameReassemblesToPayload) {
    // The framing decoder receives decrypted bytes in fragments; the frame must reassemble byte-exact.
    auto client = ObfuscatedClient::create(ObfuscatedFraming::Intermediate, 2, secret_a(), fixed_init());
    const Bytes payload(300, 0x11);
    const Bytes wire = client.frame(payload);
    Transport decoder(TransportKind::Intermediate);
    std::optional<Bytes> got;
    for (std::size_t i = 0; i < wire.size(); ++i) {
        decoder.feed(Bytes(1, wire[i]));  // one byte at a time
        if (auto packet = decoder.next()) got = std::move(*packet);
    }
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, payload);
}

// Server-to-client padded frames: the padded-intermediate writer adds 0..15 bytes to each frame and counts them in the
// length prefix. The receiver reads the frame with the normal intermediate reader and decrypts the MTProto packet. The
// packet decoder drops trailing bytes beyond the ciphertext's 16-byte blocks, so the padding does not reach the TL layer.
TEST(ObfuscatedFrame, PaddedServerFramesDecryptToTheOriginalPacket) {
    const Bytes auth_key = random_bytes(256);
    const Bytes plain_packet = encrypt_packet(auth_key, serialize_inner(InnerMessage{}), false);
    std::array<std::size_t, 16> seen{};
    for (int i = 0; i < 200; ++i) {
        auto server_side = ObfuscatedClient::create(ObfuscatedFraming::PaddedIntermediate, 2, secret_a(), fixed_init());
        const Bytes framed = server_side.frame(plain_packet);
        ASSERT_GE(framed.size(), 4 + plain_packet.size());
        ASSERT_LE(framed.size(), 4 + plain_packet.size() + 15);
        seen[framed.size() - 4 - plain_packet.size()]++;

        Transport reader(TransportKind::Intermediate);
        reader.feed(framed);
        const auto frame = reader.next();
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(decrypt_packet(auth_key, *frame, false), decrypt_packet(auth_key, plain_packet, false));
    }
    // The random pad length should cover more than one value over 200 frames.
    std::size_t distinct = 0;
    for (std::size_t n : seen) distinct += n > 0 ? 1 : 0;
    EXPECT_GT(distinct, 1u);
}

namespace {

// Builds one server-to-client padded frame for an encrypted packet, using the same writer as the client.
Bytes padded_server_frame(const Bytes& packet) {
    auto writer = ObfuscatedClient::create(ObfuscatedFraming::PaddedIntermediate, 2, secret_a(), fixed_init());
    return writer.frame(packet);
}

}  // namespace

TEST(ObfuscatedFrame, ManyPaddedServerFramesInOneReadAllDecrypt) {
    const Bytes auth_key = random_bytes(256);
    std::vector<Bytes> packets;
    Bytes stream;
    for (int i = 0; i < 20; ++i) {
        InnerMessage msg{};
        msg.message_id = 0x1000 + static_cast<std::uint64_t>(i);
        packets.push_back(encrypt_packet(auth_key, serialize_inner(msg), false));
        const Bytes framed = padded_server_frame(packets.back());
        stream.insert(stream.end(), framed.begin(), framed.end());
    }
    Transport reader(TransportKind::Intermediate);
    reader.feed(stream);
    for (int i = 0; i < 20; ++i) {
        const auto frame = reader.next();
        ASSERT_TRUE(frame.has_value()) << "frame " << i;
        EXPECT_EQ(decrypt_packet(auth_key, *frame, false), decrypt_packet(auth_key, packets[i], false)) << "frame " << i;
    }
    EXPECT_FALSE(reader.next().has_value());
}

TEST(ObfuscatedFrame, PaddedServerFrameSplitAtEveryOffsetReassembles) {
    const Bytes auth_key = random_bytes(256);
    const Bytes packet = encrypt_packet(auth_key, serialize_inner(InnerMessage{}), false);
    const Bytes framed = padded_server_frame(packet);
    const Bytes expected = decrypt_packet(auth_key, packet, false);
    for (std::size_t split = 0; split <= framed.size(); ++split) {
        Transport reader(TransportKind::Intermediate);
        reader.feed(Bytes(framed.begin(), framed.begin() + static_cast<std::ptrdiff_t>(split)));
        const auto early = reader.next();
        if (split < framed.size()) EXPECT_FALSE(early.has_value()) << "split " << split;
        reader.feed(Bytes(framed.begin() + static_cast<std::ptrdiff_t>(split), framed.end()));
        const auto frame = early ? early : reader.next();
        ASSERT_TRUE(frame.has_value()) << "split " << split;
        EXPECT_EQ(decrypt_packet(auth_key, *frame, false), expected) << "split " << split;
    }
}

TEST(ObfuscatedFrame, TruncatedPaddedFrameIsNotReturnedAsPartialData) {
    const Bytes auth_key = random_bytes(256);
    const Bytes packet = encrypt_packet(auth_key, serialize_inner(InnerMessage{}), false);
    const Bytes framed = padded_server_frame(packet);
    Transport reader(TransportKind::Intermediate);
    reader.feed(Bytes(framed.begin(), framed.end() - 1));
    EXPECT_FALSE(reader.next().has_value());
    reader.feed(Bytes(framed.end() - 1, framed.end()));
    EXPECT_TRUE(reader.next().has_value());
}

TEST(ObfuscatedFrame, ServerErrorWordInPlaceOfLengthIsRejected) {
    // Telegram sends small negative int32 codes (for example -404) where a length would be.
    Bytes stream = {0x6c, 0xfe, 0xff, 0xff};  // -404 little endian
    Transport reader(TransportKind::Intermediate);
    reader.feed(stream);
    EXPECT_THROW(reader.next(), TransportError);
}

TEST(ObfuscatedFrame, PaddingBeyondOneBlockFailsDecryption) {
    // Extra bytes that add a whole AES block are not transport padding and must fail msg_key checking.
    const Bytes auth_key = random_bytes(256);
    Bytes packet = encrypt_packet(auth_key, serialize_inner(InnerMessage{}), false);
    const Bytes block = random_bytes(16);
    packet.insert(packet.end(), block.begin(), block.end());
    const Bytes framed = padded_server_frame(packet);
    Transport reader(TransportKind::Intermediate);
    reader.feed(framed);
    const auto frame = reader.next();
    ASSERT_TRUE(frame.has_value());
    EXPECT_THROW(decrypt_packet(auth_key, *frame, false), CryptoError);
}
