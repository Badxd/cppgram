// Client::export_login_token against a scripted fake DC. Offline only. The api_hash is a dummy test value.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/mtproto/schema/api.h"
#include "cppgram/qr_login.h"
#include "fake_dc.h"

using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::LoginTokenState;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;

namespace {

std::string session_path(const char* tag) {
    static std::atomic<int> counter{0};
    return (std::filesystem::temp_directory_path() /
            ("cppgram-qr-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" + std::to_string(counter++) +
             ".session"))
        .string();
}

ClientOptions options_for(FakeDc& dc, const std::string& path, std::int32_t api_id) {
    ClientOptions o;
    o.api_id = api_id;
    o.api_hash = "00000000000000000000000000000000";  // dummy test value
    o.dc_ip = "127.0.0.1";
    o.dc_port = dc.port();
    o.dc_id = 2;
    o.server_keys = {dc.public_key()};
    o.session_path = path;
    o.connect_timeout_ms = 2000;
    return o;
}

} // namespace

TEST(QrLoginFakeDc, ExportLoginTokenSendsApiIdAndReturnsToken) {
    FakeDc dc;
    std::int32_t seen_api_id = 0;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader& args, cppgram::mtproto::TlWriter& out) {
        if (id != fn::auth_exportLoginToken::kId) return false;
        seen_api_id = args.read_int32();
        out.write_uint32(0x629f1980u);            // auth.loginToken
        out.write_int32(1800000000);              // expires
        out.write_bytes({ 0xfb, 0xff, 0x01 });    // token
        return true;
    });

    const std::string path = session_path("token");
    Client client(options_for(dc, path, 4242));
    client.connect();
    const auto info = client.export_login_token();
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_EQ(seen_api_id, 4242);
    EXPECT_EQ(info.state, LoginTokenState::Token);
    EXPECT_EQ(info.expires, 1800000000);
    EXPECT_EQ(info.token, (std::vector<std::uint8_t> { 0xfb, 0xff, 0x01 }));
    EXPECT_EQ(cppgram::qr_login_url(info.token), "tg://login?token=-_8B");
}

TEST(QrLoginFakeDc, ExportLoginTokenReturnsMigrationTarget) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter& out) {
        if (id != fn::auth_exportLoginToken::kId) return false;
        out.write_uint32(0x068e9916u);            // auth.loginTokenMigrateTo
        out.write_int32(5);                       // dc_id
        out.write_bytes({ 0x01 });
        return true;
    });

    const std::string path = session_path("migrate");
    Client client(options_for(dc, path, 4242));
    client.connect();
    const auto info = client.export_login_token();
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_EQ(info.state, LoginTokenState::MigrateTo);
    EXPECT_EQ(info.dc_id, 5);
}

TEST(QrLoginFakeDc, ExportLoginTokenRejectsMissingApiCredentials) {
    FakeDc dc;
    const std::string path = session_path("nocreds");
    Client client(options_for(dc, path, 0));
    EXPECT_THROW(client.export_login_token(), std::invalid_argument);
    std::filesystem::remove(path);
}

TEST(QrLoginFakeDc, ImportAuthorizationSendsIdAndBytesAndReturnsUserId) {
    FakeDc dc;
    std::int64_t seen_id = 0;
    std::vector<std::uint8_t> seen_bytes;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader& args, cppgram::mtproto::TlWriter& out) {
        if (id != fn::auth_importAuthorization::kId) return false;
        seen_id = args.read_int64();
        seen_bytes = args.read_bytes();
        cppgram::mtproto::tl::auth_Authorization auth;
        cppgram::mtproto::tl::types::auth_authorization ok;
        auto user = std::make_shared<cppgram::mtproto::tl::User>();
        cppgram::mtproto::tl::types::user full;
        full.id = 99887766;
        user->value = full;
        ok.user = user;
        auth.value = ok;
        auth.serialize(out);
        return true;
    });

    const std::string path = session_path("import");
    Client client(options_for(dc, path, 4242));
    client.connect();
    const std::int64_t user_id = client.import_authorization(31337, { 0xaa, 0xbb });
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_EQ(seen_id, 31337);
    EXPECT_EQ(seen_bytes, (std::vector<std::uint8_t> { 0xaa, 0xbb }));
    EXPECT_EQ(user_id, 99887766);
}

TEST(QrLoginFakeDc, ImportLoginTokenSendsTokenAndReturnsState) {
    FakeDc dc;
    std::vector<std::uint8_t> seen_token;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader& args, cppgram::mtproto::TlWriter& out) {
        if (id != fn::auth_importLoginToken::kId) return false;
        seen_token = args.read_bytes();
        out.write_uint32(0x629f1980u);  // auth.loginToken with a fresh token
        out.write_int32(1800000000);
        out.write_bytes({ 0x09 });
        return true;
    });
    const std::string path = session_path("importtok");
    Client client(options_for(dc, path, 4242));
    client.connect();
    const auto info = client.import_login_token({ 0x01, 0x02 });
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_EQ(seen_token, (std::vector<std::uint8_t> { 0x01, 0x02 }));
    EXPECT_EQ(info.state, LoginTokenState::Token);
    EXPECT_EQ(info.token, (std::vector<std::uint8_t> { 0x09 }));
}

TEST(QrLoginFakeDc, ExportAuthorizationReturnsIdAndBytes) {
    FakeDc dc;
    std::int32_t seen_dc = 0;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader& args, cppgram::mtproto::TlWriter& out) {
        if (id != fn::auth_exportAuthorization::kId) return false;
        seen_dc = args.read_int32();
        cppgram::mtproto::tl::auth_ExportedAuthorization res;
        cppgram::mtproto::tl::types::auth_exportedAuthorization ex;
        ex.id = 424242;
        ex.bytes = { 0xde, 0xad };
        res.value = ex;
        res.serialize(out);
        return true;
    });
    const std::string path = session_path("export");
    Client client(options_for(dc, path, 4242));
    client.connect();
    const auto exported = client.export_authorization(4);
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_EQ(seen_dc, 4);
    EXPECT_EQ(exported.id, 424242);
    EXPECT_EQ(exported.bytes, (std::vector<std::uint8_t> { 0xde, 0xad }));
}
