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
