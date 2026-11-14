#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <gtest/gtest.h>

#include <ctime>
#include <deque>
#include <openssl/bn.h>
#include <stdexcept>

#include "cppgram/mtproto/auth.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/schema/api.h"

#include "fake_dc.h"

using namespace cppgram::mtproto;
namespace fn = cppgram::mtproto::tl::functions;
namespace ty = cppgram::mtproto::tl::types;

using cppgram::testing::FakeServer;
using cppgram::testing::be_bytes;
using cppgram::testing::kSafePrimeHex;

TEST(Handshake, CreatesMatchingAuthKeyAgainstFakeServer) {
    FakeServer server;
    HandshakeOptions options;
    options.keys = {server.public_key()};
    const AuthKeyResult result = create_auth_key(server, options);
    EXPECT_EQ(result.auth_key.size(), 256u);
    EXPECT_EQ(result.auth_key, server.server_auth_key);
    EXPECT_EQ(result.server_salt, server.server_salt);
}

TEST(Handshake, FailsWhenNoKeyMatchesFingerprint) {
    FakeServer server;
    HandshakeOptions options;
    options.keys = {RsaPublicKey{Bytes(256, 0x11), Bytes{0x01, 0x00, 0x01}}};
    EXPECT_THROW(create_auth_key(server, options), std::exception);
}

TEST(Handshake, PlainPacketRoundTrip) {
    const Bytes body = Bytes{1, 2, 3, 4, 5, 6, 7, 8};
    const Bytes packet = wrap_plain_message(body, 0x1234500000000004ull);
    EXPECT_EQ(unwrap_plain_message(packet), body);
    EXPECT_EQ(make_message_id() % 4, 0u);
}

TEST(Handshake, AcceptsValidDhParams) {
    BIGNUM* p = BN_new();
    BN_hex2bn(&p, kSafePrimeHex);
    const Bytes prime = be_bytes(p, 256);
    BN_free(p);
    const Bytes g_a = Bytes(255, 0x01);
    EXPECT_NO_THROW(validate_dh_params(prime, 2, g_a));
}

TEST(Handshake, RejectsNonSafePrime) {
    // Composite 2048-bit value: the primality check must reject it.
    Bytes composite(256, 0x00);
    composite[0] = 0xFF;
    composite[255] = 0x01;  // odd, but composite
    EXPECT_THROW(validate_dh_params(composite, 2, Bytes(255, 0x01)), AuthError);
}

TEST(Handshake, RejectsOutOfRangeGA) {
    BIGNUM* p = BN_new();
    BN_hex2bn(&p, kSafePrimeHex);
    const Bytes prime = be_bytes(p, 256);
    BN_free(p);
    EXPECT_THROW(validate_dh_params(prime, 2, Bytes{0x01}), AuthError);  // g_a == 1
    EXPECT_THROW(validate_dh_params(prime, 2, prime), AuthError);        // g_a == p
    EXPECT_THROW(validate_dh_params(prime, 8, Bytes(255, 0x01)), AuthError);  // g outside 2..7
}

TEST(Handshake, TelegramKeysParse) {
    const auto& keys = telegram_public_keys();
    ASSERT_EQ(keys.size(), 5u);
    EXPECT_EQ(keys[0].modulus.size(), 256u);
    EXPECT_EQ(keys[0].exponent, (Bytes{0x01, 0x00, 0x01}));
}

// Servers behind MTProxy append transport padding of variable size after the plain packet. The length field is
// authoritative (TDLib behaves the same way), so any number of trailing bytes must be ignored.
TEST(Handshake, PlainPacketIgnoresTrailingPaddingOfAnySize) {
    const Bytes body = Bytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    const Bytes packet = wrap_plain_message(body, 0x1234500000000004ull);
    for (std::size_t pad : {0u, 1u, 15u, 16u, 22u, 96u, 102u, 116u, 123u, 255u, 4096u}) {
        SCOPED_TRACE("trailing " + std::to_string(pad));
        Bytes padded = packet;
        padded.insert(padded.end(), pad, 0xAB);
        EXPECT_EQ(unwrap_plain_message(padded), body);
    }
}

// Trailing counts seen in live replies through a padded (dd) MTProxy: 96, 116 and 123 bytes in the req_pq_multi and
// req_DH_params answers, plus the 22 and 102 bytes of the earlier VPS runs.
TEST(Handshake, PlainPacketRegressionLiveTrailingCounts) {
    const Bytes body(40, 0x5A);  // 40-byte plain body, the size of a req_pq_multi packet
    const Bytes packet = wrap_plain_message(body, 0x5F5E100000000001ull);
    for (std::size_t pad : {22u, 96u, 102u, 108u, 116u, 123u}) {
        SCOPED_TRACE("live trailing " + std::to_string(pad));
        Bytes reply = packet;
        reply.insert(reply.end(), pad, 0x00);
        EXPECT_EQ(unwrap_plain_message(reply), body);
    }
}

TEST(Handshake, PlainPacketRejectsLengthFieldLargerThanPacket) {
    const Bytes body = Bytes{1, 2, 3, 4, 5, 6, 7, 8};
    Bytes packet = wrap_plain_message(body, 0x1234500000000004ull);
    // Length field claims one more byte than is present.
    packet[16] = static_cast<std::uint8_t>(body.size() + 1);
    EXPECT_THROW((void)unwrap_plain_message(packet), AuthError);
}

TEST(Handshake, PlainPacketRejectsMaximumLengthFieldWithoutOverflow) {
    // A 0xFFFFFFFF length must be rejected as truncation on every platform, never wrap around or allocate.
    Bytes packet = wrap_plain_message(Bytes{1, 2}, 0x1234500000000004ull);
    packet[16] = 0xFF;
    packet[17] = 0xFF;
    packet[18] = 0xFF;
    packet[19] = 0xFF;
    EXPECT_THROW((void)unwrap_plain_message(packet), AuthError);
}

TEST(Handshake, PlainPacketRejectsNonzeroAuthKeyId) {
    Bytes packet = wrap_plain_message(Bytes{1, 2, 3, 4}, 0x1234500000000004ull);
    packet[0] = 0x01;
    EXPECT_THROW((void)unwrap_plain_message(packet), AuthError);
}

TEST(Handshake, PlainPacketRejectsShortHeader) {
    EXPECT_THROW((void)unwrap_plain_message(Bytes(19, 0x00)), AuthError);
}

TEST(Handshake, PlainPacketRejectsTruncation) {
    const Bytes body = Bytes{1, 2, 3, 4, 5, 6, 7, 8};
    Bytes packet = wrap_plain_message(body, 0x1234500000000004ull);
    packet.pop_back();  // the length field now claims one byte more than is present
    EXPECT_THROW((void)unwrap_plain_message(packet), AuthError);
}
