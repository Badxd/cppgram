#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "cppgram/mtproto/proxy_secret.h"

using namespace cppgram::mtproto;

namespace {
const std::string kKey = "0123456789abcdef0123456789ABCDEF"; // 32 hex chars = 16 bytes
}

TEST(ProxySecret, PlainSecretParsesBothCases) {
    const auto s = parse_proxy_secret(kKey);
    EXPECT_EQ(s.kind, ProxySecretKind::Plain);
    EXPECT_EQ(s.key[0], 0x01);
    EXPECT_EQ(s.key[1], 0x23);
    EXPECT_EQ(s.key[15], 0xEF);
    EXPECT_TRUE(s.fake_tls_domain.empty());
}

TEST(ProxySecret, PaddedIntermediateSecret) {
    const auto s = parse_proxy_secret("dd" + kKey);
    EXPECT_EQ(s.kind, ProxySecretKind::PaddedIntermediate);
    EXPECT_EQ(s.key[0], 0x01);
    EXPECT_EQ(s.key[15], 0xEF);
}

TEST(ProxySecret, FakeTlsSecretCarriesDomain) {
    // "a.b" as hex: 61 2e 62
    const auto s = parse_proxy_secret("ee" + kKey + "612e62");
    EXPECT_EQ(s.kind, ProxySecretKind::FakeTls);
    EXPECT_EQ(s.key[15], 0xEF);
    EXPECT_EQ(s.fake_tls_domain, "a.b");
}

TEST(ProxySecret, RejectsWrongLengths) {
    EXPECT_THROW(parse_proxy_secret(""), std::invalid_argument);
    EXPECT_THROW(parse_proxy_secret(kKey.substr(0, 31)), std::invalid_argument);
    EXPECT_THROW(parse_proxy_secret(kKey + "0"), std::invalid_argument); // 33 chars, no prefix
    EXPECT_THROW(parse_proxy_secret("dd" + kKey + "00"), std::invalid_argument); // dd with extra bytes
    EXPECT_THROW(parse_proxy_secret("dd" + kKey.substr(0, 31)), std::invalid_argument); // 33 chars
    // "dd" plus 30 chars is 32 chars total, which is a valid plain secret, so it must parse as Plain.
    EXPECT_EQ(parse_proxy_secret("dd" + kKey.substr(0, 30)).kind, ProxySecretKind::Plain);
}

TEST(ProxySecret, RejectsNonHex) {
    std::string bad = kKey;
    bad[5] = 'g';
    EXPECT_THROW(parse_proxy_secret(bad), std::invalid_argument);
    bad = kKey;
    bad[0] = ' ';
    EXPECT_THROW(parse_proxy_secret(bad), std::invalid_argument);
}

TEST(ProxySecret, RejectsUnknownPrefix) {
    EXPECT_THROW(parse_proxy_secret("ff" + kKey), std::invalid_argument);
    EXPECT_THROW(parse_proxy_secret("00" + kKey), std::invalid_argument);
}

TEST(ProxySecret, FakeTlsNeedsDomain) {
    EXPECT_THROW(parse_proxy_secret("ee" + kKey), std::invalid_argument); // no domain bytes
    EXPECT_THROW(parse_proxy_secret("ee" + kKey + "6"), std::invalid_argument); // odd domain hex
    EXPECT_THROW(parse_proxy_secret("ee" + kKey + "zz"), std::invalid_argument);
}

TEST(ProxySecret, FakeTlsDomainRules) {
    // Control character 0x0a in the domain.
    EXPECT_THROW(parse_proxy_secret("ee" + kKey + "610a62"), std::invalid_argument);
    // Space (0x20) is not allowed.
    EXPECT_THROW(parse_proxy_secret("ee" + kKey + "612062"), std::invalid_argument);
    // 254 bytes is over the limit, 253 is allowed.
    const std::string too_long(254 * 2, '6');
    EXPECT_THROW(parse_proxy_secret("ee" + kKey + too_long), std::invalid_argument);
    const std::string max_len(253 * 2, '6');
    EXPECT_EQ(parse_proxy_secret("ee" + kKey + max_len).fake_tls_domain.size(), 253u);
}

TEST(ProxySecret, ErrorMessageNeverEchoesSecret) {
    std::string bad = kKey;
    bad[7] = 'x';
    try {
        parse_proxy_secret(bad);
        FAIL() << "expected rejection";
    } catch (const std::invalid_argument& e) {
        const std::string msg = e.what();
        EXPECT_EQ(msg.find(kKey), std::string::npos);
        EXPECT_EQ(msg.find(bad), std::string::npos);
    }
}

// ---------- Base64 and Base64URL forms ----------------------------------------------------------------------------
// Synthetic vectors only. The key is made up, and the expected strings were produced by Python's base64 module, which
// is independent of cppgram. Each vector has a different standard and URL-safe text, so a wrong alphabet fails.

namespace {
const std::array<std::uint8_t, 16> kSynthKey
    = { 0xfb, 0xff, 0xbe, 0x3e, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb };

// The error text must not contain the input, or any part of it that is longer than a few characters.
void expect_rejected_without_echo(const std::string& input) {
    std::string message;
    bool threw = false;
    try {
        (void)parse_proxy_secret(input);
    } catch (const std::invalid_argument& e) {
        threw = true;
        message = e.what();
    }
    EXPECT_TRUE(threw) << "accepted: " << input.size() << " characters";
    if (input.size() >= 8)
        EXPECT_EQ(message.find(input), std::string::npos) << "error text echoes the input";
}
} // namespace

TEST(ProxySecretBase64, PlainSecretInBase64URLPaddedAndUnpadded) {
    for (const char* text : { "-_--PgARIjNEVWZ3iJmquw==", "-_--PgARIjNEVWZ3iJmquw" }) {
        SCOPED_TRACE(text);
        const auto s = parse_proxy_secret(text);
        EXPECT_EQ(s.kind, ProxySecretKind::Plain);
        EXPECT_EQ(s.key, kSynthKey);
        EXPECT_EQ(s.encoding, ProxySecretEncoding::Base64Url);
        EXPECT_TRUE(s.fake_tls_domain.empty());
    }
}

TEST(ProxySecretBase64, PlainSecretInStandardBase64PaddedAndUnpadded) {
    for (const char* text : { "+/++PgARIjNEVWZ3iJmquw==", "+/++PgARIjNEVWZ3iJmquw" }) {
        SCOPED_TRACE(text);
        const auto s = parse_proxy_secret(text);
        EXPECT_EQ(s.kind, ProxySecretKind::Plain);
        EXPECT_EQ(s.key, kSynthKey);
        EXPECT_EQ(s.encoding, ProxySecretEncoding::Base64);
    }
}

TEST(ProxySecretBase64, PaddedIntermediateSecretInBothAlphabets) {
    const auto url = parse_proxy_secret("3fv_vj4AESIzRFVmd4iZqrs=");
    EXPECT_EQ(url.kind, ProxySecretKind::PaddedIntermediate);
    EXPECT_EQ(url.key, kSynthKey); // the 0xdd prefix byte is dropped
    EXPECT_EQ(url.encoding, ProxySecretEncoding::Base64Url);

    const auto std_form = parse_proxy_secret("3fv/vj4AESIzRFVmd4iZqrs=");
    EXPECT_EQ(std_form.kind, ProxySecretKind::PaddedIntermediate);
    EXPECT_EQ(std_form.key, kSynthKey);
    EXPECT_EQ(std_form.encoding, ProxySecretEncoding::Base64);
}

TEST(ProxySecretBase64, FakeTlsSecretKeepsTheDomain) {
    for (const char* text :
         { "7vv_vj4AESIzRFVmd4iZqrtleGFtcGxlLmNvbQ==", "7vv/vj4AESIzRFVmd4iZqrtleGFtcGxlLmNvbQ==" }) {
        SCOPED_TRACE(text);
        const auto s = parse_proxy_secret(text);
        EXPECT_EQ(s.kind, ProxySecretKind::FakeTls);
        EXPECT_EQ(s.key, kSynthKey);
        EXPECT_EQ(s.fake_tls_domain, "example.com");
    }
}

TEST(ProxySecretBase64, AlphabetIsStrict) {
    // URL-safe characters are invalid in the standard alphabet and the reverse. Both fail, so nothing is accepted.
    expect_rejected_without_echo("+_--PgARIjNEVWZ3iJmquw");
    expect_rejected_without_echo("-/--PgARIjNEVWZ3iJmquw");
}

TEST(ProxySecretBase64, MalformedBase64IsRejected) {
    expect_rejected_without_echo("-_--PgARIjNEVWZ3iJmquw "); // trailing space
    expect_rejected_without_echo("-_--PgARIjNEVWZ3iJmquw\n"); // trailing line break
    expect_rejected_without_echo("-_--Pg=RIjNEVWZ3iJmquw=="); // '=' in the middle
    expect_rejected_without_echo("-_--PgARIjNEVWZ3iJmquw==="); // three '=' characters
    expect_rejected_without_echo("-_--PgARIjNEVWZ3iJmquw="); // wrong padding for the length
    expect_rejected_without_echo("-_--PgARIjNEVWZ3iJmqu"); // 21 characters: length mod 4 == 1, never valid
    expect_rejected_without_echo("a"); // one character can never be valid
    expect_rejected_without_echo("-_--PgARIjNEVWZ3iJmqux"); // non-zero trailing bits
}

TEST(ProxySecretBase64, DecodedLengthAndPrefixAreChecked) {
    expect_rejected_without_echo("-_--PgARIjNEVWZ3iJmq"); // 15 bytes
    expect_rejected_without_echo("qvv_vj4AESIzRFVmd4iZqrs="); // 17 bytes, no 0xdd or 0xee prefix
    expect_rejected_without_echo("3fv_vj4AESIzRFVmd4iZqrt4"); // 18 bytes starting 0xdd
    expect_rejected_without_echo("7vv_vj4AESIzRFVmd4iZqrs="); // 0xee with no domain
    // 0xee + key + "x" (18 bytes) with one extra zero-bit character: 25 characters, length mod 4 == 1.
    expect_rejected_without_echo("7vv_vj4AESIzRFVmd4iZqrt4A");
}

TEST(ProxySecretBase64, FakeTlsDomainLimitsApplyToBase64) {
    // 0xee + key + 254 printable bytes: one byte over the 253-byte limit.
    expect_rejected_without_echo(
        "7vv_"
        "vj4AESIzRFVmd4iZqrthYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYW"
        "FhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFh"
        "YWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYW"
        "FhYWFhYWFhYWFhYWFhYWFhYWFhYQ==");
}

TEST(ProxySecretBase64, OversizedTextIsRejectedBeforeDecoding) {
    expect_rejected_without_echo(std::string(541, 'A'));
}

TEST(ProxySecretBase64, EvenLengthAllHexTextIsReadAsHex) {
    // 22 hex digits is also a valid Base64 length for 16 bytes. TDLib reads an even-length all-hex string as hex, so
    // this is rejected (decodes to 11 bytes), not reread as Base64. This is deliberate. See KNOWN_LIMITATIONS.md.
    expect_rejected_without_echo("aaaaaaaaaaaaaaaaaaaaaa");
}

TEST(ProxySecretBase64, WhitespaceIsNeverTrimmed) {
    expect_rejected_without_echo(" -_--PgARIjNEVWZ3iJmquw");
    expect_rejected_without_echo("3fv_vj4AESIzRFVmd4iZqrs= ");
}

TEST(ProxySecretEncoding, HexFormsReportHex) {
    EXPECT_EQ(parse_proxy_secret(kKey).encoding, ProxySecretEncoding::Hex);
    EXPECT_EQ(parse_proxy_secret("dd" + kKey).encoding, ProxySecretEncoding::Hex);
    EXPECT_EQ(parse_proxy_secret("ee" + kKey + "612e62").encoding, ProxySecretEncoding::Hex);
}

TEST(ProxySecretRegression, ExistingHexRejectionsStillHold) {
    // These were rejected before Base64 support and must still be rejected. The reason may differ, the outcome must not.
    expect_rejected_without_echo(kKey + "0"); // 33 characters
    expect_rejected_without_echo("dd" + kKey + "00"); // dd with too many bytes
    expect_rejected_without_echo("ee" + kKey); // fake-TLS without a domain
    expect_rejected_without_echo("ee" + kKey + "610a62"); // control character in the domain
}
