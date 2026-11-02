#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "cppgram/session_crypto.h"

using namespace cppgram::session_crypto;

namespace {

std::vector<std::uint8_t> bytes_of(const std::string& s) { return {s.begin(), s.end()}; }

}  // namespace

TEST(SessionCrypto, RoundTripRestoresPlaintext) {
    const auto plain = bytes_of("auth key bytes and dc id 2");
    const auto blob = encrypt("correct horse battery staple", plain);
    EXPECT_TRUE(is_encrypted(blob));
    EXPECT_EQ(decrypt("correct horse battery staple", blob), plain);
}

TEST(SessionCrypto, EmptyPlaintextRoundTrips) {
    const auto blob = encrypt("pw", {});
    EXPECT_TRUE(decrypt("pw", blob).empty());
}

TEST(SessionCrypto, CiphertextDoesNotContainPlaintext) {
    const auto plain = bytes_of("SECRET-SESSION-MARKER-0123456789");
    const auto blob = encrypt("pw", plain);
    const auto hit = std::search(blob.begin(), blob.end(), plain.begin(), plain.end());
    EXPECT_EQ(hit, blob.end());
}

TEST(SessionCrypto, EveryEncryptionUsesFreshSaltAndNonce) {
    const auto plain = bytes_of("same input");
    const auto a = encrypt("pw", plain);
    const auto b = encrypt("pw", plain);
    EXPECT_NE(a, b);
    // Salt and nonce sit at fixed offsets: magic(4)+params(3), then salt(16), nonce(12).
    EXPECT_NE(std::vector<std::uint8_t>(a.begin() + 7, a.begin() + 35), std::vector<std::uint8_t>(b.begin() + 7, b.begin() + 35));
    EXPECT_NE(std::vector<std::uint8_t>(a.begin() + 35, a.begin() + 47), std::vector<std::uint8_t>(b.begin() + 35, b.begin() + 47));
}

TEST(SessionCrypto, WrongPassphraseIsRejected) {
    const auto blob = encrypt("right", bytes_of("data"));
    EXPECT_THROW(decrypt("wrong", blob), std::runtime_error);
}

TEST(SessionCrypto, EveryByteFlipIsDetected) {
    const auto blob = encrypt("pw", bytes_of("integrity matters"));
    // Flip one bit at every position: header, salt, nonce, tag and ciphertext must all fail authentication.
    for (std::size_t i = 0; i < blob.size(); ++i) {
        auto bad = blob;
        bad[i] ^= 0x01;
        EXPECT_ANY_THROW(decrypt("pw", bad)) << "byte " << i << " not protected";
    }
}

TEST(SessionCrypto, TruncationIsDetected) {
    const auto blob = encrypt("pw", bytes_of("some session data"));
    for (std::size_t len : {std::size_t{0}, std::size_t{10}, blob.size() - 1}) {
        std::vector<std::uint8_t> cut(blob.begin(), blob.begin() + static_cast<std::ptrdiff_t>(len));
        EXPECT_ANY_THROW(decrypt("pw", cut)) << "length " << len;
    }
}

TEST(SessionCrypto, PlainSessionIsNotMistakenForEncrypted) {
    const auto plain = bytes_of("CGS2 plain session file");
    EXPECT_FALSE(is_encrypted(plain));
    EXPECT_THROW(decrypt("pw", plain), std::runtime_error);
}

TEST(SessionCrypto, OutOfRangeCostParametersAreRejected) {
    auto blob = encrypt("pw", bytes_of("x"));
    blob[4] = 3;  // log2N far too small: would be a downgrade
    EXPECT_THROW(decrypt("pw", blob), std::runtime_error);
    blob[4] = 40;  // log2N far too large: would be a memory-exhaustion request
    EXPECT_THROW(decrypt("pw", blob), std::runtime_error);
}

TEST(SessionCrypto, EmptyPassphraseIsRefused) {
    EXPECT_THROW(encrypt("", bytes_of("x")), std::invalid_argument);
    EXPECT_THROW(decrypt("", encrypt("pw", bytes_of("x"))), std::invalid_argument);
}

TEST(SessionCrypto, ErrorMessageDoesNotEchoSecrets) {
    const auto blob = encrypt("hunter2", bytes_of("payload"));
    try {
        decrypt("not-hunter2", blob);
        FAIL() << "expected failure";
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_EQ(msg.find("hunter2"), std::string::npos);
        EXPECT_EQ(msg.find("payload"), std::string::npos);
    }
}
