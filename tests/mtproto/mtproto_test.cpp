#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/tl.h"
#include "cppgram/mtproto/transport.h"

using namespace cppgram::mtproto;

namespace {

Bytes text(const std::string& s) {
    return Bytes(s.begin(), s.end());
}

}  // namespace

TEST(Crypto, Crc32KnownVector) {
    EXPECT_EQ(crc32(text("123456789")), 0xCBF43926u);
}

TEST(Crypto, Sha1AndSha256KnownVectors) {
    EXPECT_EQ(sha1(text("abc")),
              (Bytes{0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                     0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d}));
    EXPECT_EQ(sha256(text("abc"))[0], 0xba);
    EXPECT_EQ(sha256(text("abc"))[31], 0xad);
}

TEST(Crypto, AesIgeRoundTrip) {
    const Bytes key = random_bytes(32);
    const Bytes iv = random_bytes(32);
    const Bytes plain = random_bytes(16 * 9);
    const Bytes cipher = aes_ige_encrypt(plain, key, iv);
    EXPECT_NE(cipher, plain);
    EXPECT_EQ(aes_ige_decrypt(cipher, key, iv), plain);
}

TEST(Crypto, AesIgeRejectsBadSizes) {
    EXPECT_THROW(aes_ige_encrypt(Bytes(15, 0), Bytes(32, 0), Bytes(32, 0)), CryptoError);
    EXPECT_THROW(aes_ige_encrypt(Bytes(16, 0), Bytes(16, 0), Bytes(32, 0)), CryptoError);
}

TEST(Crypto, InnerMessageRoundTrip) {
    InnerMessage msg;
    msg.salt = 0x1122334455667788ull;
    msg.session_id = 42;
    msg.message_id = 0x6000000000000001ull;
    msg.seq_no = 3;
    msg.body = text("hello");
    const InnerMessage parsed = parse_inner(serialize_inner(msg));
    EXPECT_EQ(parsed.salt, msg.salt);
    EXPECT_EQ(parsed.session_id, msg.session_id);
    EXPECT_EQ(parsed.message_id, msg.message_id);
    EXPECT_EQ(parsed.seq_no, msg.seq_no);
    EXPECT_EQ(parsed.body, msg.body);
}

TEST(Crypto, PacketRoundTripBothDirections) {
    const Bytes auth_key = random_bytes(256);
    InnerMessage msg;
    msg.message_id = 1;
    msg.body = random_bytes(100);
    const Bytes inner = serialize_inner(msg);

    const Bytes out_packet = encrypt_packet(auth_key, inner, true);
    EXPECT_EQ(slice(out_packet, 0, 8), auth_key_id(auth_key));
    const InnerMessage back = parse_inner(decrypt_packet(auth_key, out_packet, true));
    EXPECT_EQ(back.body, msg.body);

    const Bytes in_packet = encrypt_packet(auth_key, inner, false);
    EXPECT_NO_THROW(decrypt_packet(auth_key, in_packet, false));
    // Decrypting with the wrong direction must fail the msg_key check.
    EXPECT_THROW(decrypt_packet(auth_key, in_packet, true), CryptoError);
}

TEST(Crypto, PacketTamperingDetected) {
    const Bytes auth_key = random_bytes(256);
    Bytes packet = encrypt_packet(auth_key, serialize_inner(InnerMessage{}), true);
    packet.back() ^= 0x01;
    EXPECT_THROW(decrypt_packet(auth_key, packet, true), CryptoError);
}

TEST(Crypto, FactorizePqFromTelegramDocs) {
    const auto [p, q] = factorize_pq(1724114033281923457ull);
    EXPECT_EQ(p, 1229739323ull);
    EXPECT_EQ(q, 1402015859ull);
}

TEST(Crypto, DhPowerSmallExample) {
    EXPECT_EQ(dh_power(Bytes{0x02}, Bytes{0x0a}, Bytes{0x03, 0xe8}), (Bytes{0x00, 0x18}));
}

TEST(Crypto, RsaHandshakeOutputSize) {
    const Bytes modulus(256, 0x01);
    Bytes n = modulus;
    n[0] = 0x7f;
    const Bytes e{0x01, 0x00, 0x01};
    const Bytes out = rsa_encrypt_handshake(text("payload"), n, e);
    EXPECT_EQ(out.size(), 256u);
    EXPECT_NE(rsa_fingerprint(n, e), 0u);
}

TEST(Tl, BytesEncodingShortAndLong) {
    TlWriter w;
    w.write_bytes(text("abc"));
    EXPECT_EQ(w.bytes(), (Bytes{3, 'a', 'b', 'c'}));

    TlWriter long_w;
    long_w.write_bytes(Bytes(300, 0x41));
    EXPECT_EQ(long_w.bytes()[0], 0xfe);
    EXPECT_EQ(long_w.bytes().size(), 4u + 300u + 0u);
}

TEST(Tl, RoundTripAllPrimitives) {
    TlWriter w;
    w.write_int32(-5);
    w.write_int64(-1234567890123ll);
    w.write_bool(true);
    w.write_bool(false);
    w.write_string("cppgram");
    w.write_bytes(Bytes(400, 0x7));
    w.write_int128(Bytes(16, 0x9));
    w.write_int256(Bytes(32, 0x3));
    w.write_vector_header(2);
    w.write_uint64(7);
    w.write_uint64(8);

    TlReader r(w.bytes());
    EXPECT_EQ(r.read_int32(), -5);
    EXPECT_EQ(r.read_int64(), -1234567890123ll);
    EXPECT_TRUE(r.read_bool());
    EXPECT_FALSE(r.read_bool());
    EXPECT_EQ(r.read_string(), "cppgram");
    EXPECT_EQ(r.read_bytes(), Bytes(400, 0x7));
    EXPECT_EQ(r.read_int128(), Bytes(16, 0x9));
    EXPECT_EQ(r.read_int256(), Bytes(32, 0x3));
    EXPECT_EQ(r.read_uint32(), kVectorId);
    EXPECT_EQ(r.read_uint32(), 2u);
    EXPECT_EQ(r.read_uint64(), 7u);
    EXPECT_EQ(r.read_uint64(), 8u);
    EXPECT_TRUE(r.at_end());
}

TEST(Tl, ReaderRejectsTruncatedData) {
    TlReader r(Bytes{0x01, 0x02});
    EXPECT_THROW(r.read_int32(), TlError);
}

TEST(Transport, AbridgedShortAndLongFrames) {
    Transport tx(TransportKind::Abridged);
    EXPECT_EQ(tx.init_bytes(), (Bytes{0xef}));
    const Bytes small = Bytes(8, 0x11);
    const Bytes big = Bytes(4 * 200, 0x22);

    Bytes wire = tx.encode(small);
    EXPECT_EQ(wire[0], 2);
    const Bytes big_wire = tx.encode(big);
    EXPECT_EQ(big_wire[0], 0x7f);
    wire.insert(wire.end(), big_wire.begin(), big_wire.end());

    Transport rx(TransportKind::Abridged);
    // Feed one byte at a time to exercise partial frames.
    std::optional<Bytes> first;
    std::optional<Bytes> second;
    for (const std::uint8_t b : wire) {
        rx.feed(Bytes{b});
        if (!first) {
            first = rx.next();
        } else if (!second) {
            second = rx.next();
        }
    }
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, small);
    EXPECT_EQ(*second, big);
}

TEST(Transport, AbridgedRejectsUnalignedPayload) {
    Transport tx(TransportKind::Abridged);
    EXPECT_THROW(tx.encode(Bytes(5, 0)), TransportError);
}

TEST(Transport, IntermediateRoundTrip) {
    Transport tx(TransportKind::Intermediate);
    EXPECT_EQ(tx.init_bytes(), (Bytes{0xee, 0xee, 0xee, 0xee}));
    const Bytes payload = text("intermediate payload");
    Transport rx(TransportKind::Intermediate);
    rx.feed(tx.encode(payload));
    const auto got = rx.next();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, payload);
    EXPECT_FALSE(rx.next().has_value());
}

TEST(Transport, FullRoundTripAndCrcCheck) {
    Transport tx(TransportKind::Full);
    EXPECT_TRUE(tx.init_bytes().empty());
    const Bytes payload = text("full transport");
    Bytes wire = tx.encode(payload);
    const Bytes second = tx.encode(payload);
    wire.insert(wire.end(), second.begin(), second.end());

    Transport rx(TransportKind::Full);
    rx.feed(wire);
    EXPECT_EQ(rx.next(), payload);
    EXPECT_EQ(rx.next(), payload);

    Bytes corrupted = tx.encode(payload);
    corrupted[10] ^= 0xff;
    Transport bad(TransportKind::Full);
    bad.feed(corrupted);
    EXPECT_THROW(bad.next(), TransportError);
}

#include "cppgram/mtproto/schema/api.h"

TEST(Schema, ReqPqMultiSerializesWithTelegramId) {
    cppgram::mtproto::tl::functions::req_pq_multi req;
    req.nonce = Bytes(16, 0xAB);
    TlWriter w;
    req.serialize(w);
    ASSERT_EQ(w.bytes().size(), 4u + 16u);
    EXPECT_EQ(TlReader(w.bytes()).read_uint32(), 0xbe7e8ef1u);
    EXPECT_EQ(slice(w.bytes(), 4, 16), Bytes(16, 0xAB));
}

TEST(Schema, ResPqReadsFromWire) {
    TlWriter w;
    w.write_uint32(0x05162463u);       // resPQ
    w.write_raw(Bytes(16, 0x01));      // nonce
    w.write_raw(Bytes(16, 0x02));      // server_nonce
    w.write_bytes(Bytes{0x17, 0xED, 0x48, 0x94, 0x1A, 0x08, 0xF9, 0x81});  // pq
    w.write_vector_header(1);
    w.write_uint64(0x1122334455667788ull);  // fingerprint
    TlReader r(w.bytes());
    const auto res = cppgram::mtproto::tl::ResPQ::read(r);
    const auto* ok = std::get_if<cppgram::mtproto::tl::types::resPQ>(&res.value);
    ASSERT_NE(ok, nullptr);
    EXPECT_EQ(ok->nonce, Bytes(16, 0x01));
    EXPECT_EQ(ok->server_nonce, Bytes(16, 0x02));
    ASSERT_EQ(ok->server_public_key_fingerprints.size(), 1u);
    EXPECT_TRUE(r.at_end());
}

TEST(CdnCrypto, CtrRoundTripAndCounterIv) {
    using cppgram::mtproto::Bytes;
    const Bytes key(32, 0x11);
    const Bytes base_iv(16, 0x00);
    Bytes data(100);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(i);
    const Bytes enc = cppgram::mtproto::aes_ctr_crypt(data, key, cppgram::mtproto::cdn_counter_iv(base_iv, 4096));
    EXPECT_NE(enc, data);
    EXPECT_EQ(cppgram::mtproto::aes_ctr_crypt(enc, key, cppgram::mtproto::cdn_counter_iv(base_iv, 4096)), data);

    const Bytes iv = cppgram::mtproto::cdn_counter_iv(Bytes(16, 0xAA), 32);  // block 2
    EXPECT_EQ(iv[12], 0);
    EXPECT_EQ(iv[13], 0);
    EXPECT_EQ(iv[14], 0);
    EXPECT_EQ(iv[15], 2);
    EXPECT_EQ(iv[0], 0xAA);
}
