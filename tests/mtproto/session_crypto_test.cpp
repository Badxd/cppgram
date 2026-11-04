#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "cppgram/session_crypto.h"
#include "session_crypto_detail.h"

using namespace cppgram::session_crypto;
namespace detail = cppgram::session_crypto::detail;

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

// ---- bounds and seams (regression tests for the resource and validation review) ----

// Regression: before the bound was lowered, a header asking for N = 2^20, r = 16 was accepted, which needs
// about 2 GiB of scrypt memory from an untrusted file. The bound is now 128 MiB. Rejection happens before any
// allocation, so these checks are fast.
TEST(SessionCrypto, ScryptCostAboveTheMemoryBoundIsRejectedBeforeDerivation) {
    auto blob = encrypt("pw", bytes_of("x"));
    blob[4] = 18;  // N = 2^18 with r = 8 is 256 MiB
    EXPECT_THROW(decrypt("pw", blob), std::runtime_error);
    blob[4] = 15;
    blob[5] = 16;  // r = 16 doubles memory again
    EXPECT_THROW(decrypt("pw", blob), std::runtime_error);
    blob[5] = 0;
    EXPECT_THROW(decrypt("pw", blob), std::runtime_error);
}

TEST(SessionCrypto, OversizedBlobIsRejectedBeforeAnyWork) {
    std::vector<std::uint8_t> huge(16u * 1024u * 1024u + 1, 0);
    std::copy_n(kMagic.data(), 4, huge.begin());
    huge[4] = 15;
    huge[5] = 8;
    huge[6] = 1;
    EXPECT_THROW(decrypt("pw", huge), std::runtime_error);
}

TEST(SessionCrypto, OpenWithKeyNeedsTheRightKeyAndAuthenticatesEveryByte) {
    const auto plain = bytes_of("open-with-key payload");
    const auto blob = encrypt("pw", plain);
    const auto key = detail::derive_key_for("pw", blob);
    ASSERT_EQ(key.size(), 32u);
    EXPECT_EQ(detail::open_with_key(key.data(), blob), plain);

    auto wrong = key;
    wrong[0] ^= 0x01;
    EXPECT_THROW(detail::open_with_key(wrong.data(), blob), std::runtime_error);

    auto tampered = blob;
    tampered.back() ^= 0x80;  // ciphertext byte
    EXPECT_THROW(detail::open_with_key(key.data(), tampered), std::runtime_error);
    tampered = blob;
    tampered[7] ^= 0x01;  // salt is covered by the key, so this changes the derived key and fails
    EXPECT_THROW(detail::open_with_key(key.data(), tampered), std::runtime_error);
}

TEST(SessionCrypto, EveryTruncationLengthIsRejected) {
    const auto blob = encrypt("pw", bytes_of("truncate me"));
    const auto key = detail::derive_key_for("pw", blob);
    for (std::size_t n = 0; n < blob.size(); ++n) {
        std::vector<std::uint8_t> cut(blob.begin(), blob.begin() + static_cast<std::ptrdiff_t>(n));
        EXPECT_THROW(detail::open_with_key(key.data(), cut), std::runtime_error) << "len " << n;
    }
}

TEST(SessionCrypto, HeaderValidatorAcceptsWhatTheWriterProduces) {
    const auto blob = encrypt("pw", bytes_of("writer output"));
    EXPECT_NO_THROW(detail::validate_header(blob));
}
