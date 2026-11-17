// Offline tests for QR login and authorization transfer helpers. No network access.
#include <gtest/gtest.h>

#include "cppgram/mtproto/schema/api.h"
#include "cppgram/mtproto/tl.h"
#include "cppgram/qr_login.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using cppgram::LoginTokenState;
namespace tl = cppgram::mtproto::tl;
namespace ty = cppgram::mtproto::tl::types;

std::vector<std::uint8_t> bytes_of(const std::string& s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& b, std::size_t off) {
    return std::uint32_t(b[off]) | (std::uint32_t(b[off + 1]) << 8) | (std::uint32_t(b[off + 2]) << 16)
        | (std::uint32_t(b[off + 3]) << 24);
}

std::shared_ptr<tl::User> user_with_id(std::int64_t id) {
    auto u = std::make_shared<tl::User>();
    ty::user full;
    full.id = id;
    u->value = full;
    return u;
}

} // namespace

// RFC 4648 section 10 test vectors, base64url without padding.
TEST(QrLogin, Base64UrlMatchesRfcVectors) {
    EXPECT_EQ(cppgram::base64url_encode({}), "");
    EXPECT_EQ(cppgram::base64url_encode(bytes_of("f")), "Zg");
    EXPECT_EQ(cppgram::base64url_encode(bytes_of("fo")), "Zm8");
    EXPECT_EQ(cppgram::base64url_encode(bytes_of("foo")), "Zm9v");
    EXPECT_EQ(cppgram::base64url_encode(bytes_of("foob")), "Zm9vYg");
    EXPECT_EQ(cppgram::base64url_encode(bytes_of("fooba")), "Zm9vYmE");
    EXPECT_EQ(cppgram::base64url_encode(bytes_of("foobar")), "Zm9vYmFy");
}

TEST(QrLogin, Base64UrlUsesUrlSafeAlphabet) {
    // 0xfb 0xff standard base64 is "+/8"; the url-safe alphabet uses "-" and "_".
    EXPECT_EQ(cppgram::base64url_encode({ 0xfb, 0xff }), "-_8");
}

TEST(QrLogin, LoginUrlHasTgLoginPrefixAndEncodedToken) {
    EXPECT_EQ(cppgram::qr_login_url(bytes_of("foobar")), "tg://login?token=Zm9vYmFy");
    EXPECT_EQ(cppgram::qr_login_url({}), "tg://login?token=");
}

TEST(QrLogin, LoginTokenVariantIsParsed) {
    tl::auth_LoginToken value;
    ty::auth_loginToken t;
    t.expires = 1700000000;
    t.token = bytes_of("tok");
    value.value = t;

    const auto info = cppgram::login_token_from_tl(value);
    EXPECT_EQ(info.state, LoginTokenState::Token);
    EXPECT_EQ(info.token, bytes_of("tok"));
    EXPECT_EQ(info.expires, 1700000000);
    EXPECT_FALSE(info.authorized_user_id.has_value());
}

TEST(QrLogin, MigrateToVariantKeepsDataCenter) {
    tl::auth_LoginToken value;
    ty::auth_loginTokenMigrateTo m;
    m.dc_id = 4;
    m.token = bytes_of("mig");
    value.value = m;

    const auto info = cppgram::login_token_from_tl(value);
    EXPECT_EQ(info.state, LoginTokenState::MigrateTo);
    EXPECT_EQ(info.dc_id, 4);
    EXPECT_EQ(info.token, bytes_of("mig"));
}

TEST(QrLogin, SuccessVariantReturnsAuthorizedUserId) {
    tl::auth_LoginToken value;
    ty::auth_loginTokenSuccess s;
    auto auth = std::make_shared<tl::auth_Authorization>();
    ty::auth_authorization ok;
    ok.user = user_with_id(777000123);
    auth->value = ok;
    s.authorization = auth;
    value.value = s;

    const auto info = cppgram::login_token_from_tl(value);
    EXPECT_EQ(info.state, LoginTokenState::Success);
    ASSERT_TRUE(info.authorized_user_id.has_value());
    EXPECT_EQ(*info.authorized_user_id, 777000123);
}

TEST(QrLogin, SuccessWithSignUpRequiredThrows) {
    tl::auth_LoginToken value;
    ty::auth_loginTokenSuccess s;
    auto auth = std::make_shared<tl::auth_Authorization>();
    auth->value = ty::auth_authorizationSignUpRequired{};
    s.authorization = auth;
    value.value = s;

    EXPECT_THROW(cppgram::login_token_from_tl(value), std::runtime_error);
}

TEST(QrLogin, SuccessWithoutAuthorizationThrows) {
    tl::auth_LoginToken value;
    value.value = ty::auth_loginTokenSuccess{};
    EXPECT_THROW(cppgram::login_token_from_tl(value), std::runtime_error);
}

TEST(QrLogin, RequestConstructorIdsMatchLayer233) {
    // exportLoginToken, importLoginToken, exportAuthorization, importAuthorization.
    EXPECT_EQ(tl::functions::auth_exportLoginToken::kId, 0xb7e085feu);
    EXPECT_EQ(tl::functions::auth_importLoginToken::kId, 0x95ac5ce4u);
    EXPECT_EQ(tl::functions::auth_exportAuthorization::kId, 0xe5bfffcdu);
    EXPECT_EQ(tl::functions::auth_importAuthorization::kId, 0xa57a7dadu);
}

TEST(QrLogin, ExportLoginTokenSerializesIdFirst) {
    tl::functions::auth_exportLoginToken req;
    req.api_id = 12345;
    req.api_hash = "hash";
    cppgram::mtproto::TlWriter writer;
    req.serialize(writer);
    const auto& b = writer.bytes();
    ASSERT_GE(b.size(), 8u);
    EXPECT_EQ(read_u32(b, 0), 0xb7e085feu);
    EXPECT_EQ(read_u32(b, 4), 12345u);
}
