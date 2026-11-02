#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "cppgram/mtproto/proxy_secret.h"

using namespace cppgram::mtproto;

namespace {
const std::string kKey = "0123456789abcdef0123456789ABCDEF";  // 32 hex chars = 16 bytes
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
    EXPECT_THROW(parse_proxy_secret(kKey + "0"), std::invalid_argument);       // 33 chars, no prefix
    EXPECT_THROW(parse_proxy_secret("dd" + kKey + "00"), std::invalid_argument);  // dd with extra bytes
    EXPECT_THROW(parse_proxy_secret("dd" + kKey.substr(0, 31)), std::invalid_argument);  // 33 chars
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
    EXPECT_THROW(parse_proxy_secret("ee" + kKey), std::invalid_argument);       // no domain bytes
    EXPECT_THROW(parse_proxy_secret("ee" + kKey + "6"), std::invalid_argument);  // odd domain hex
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
