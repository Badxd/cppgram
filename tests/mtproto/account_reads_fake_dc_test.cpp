// Read-only Premium, app config and saved music calls against a scripted fake DC. Offline only. NOT RUN in this session.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

#include "cppgram/client.h"
#include "cppgram/mtproto/schema/api.h"
#include "fake_dc.h"

using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::PeerKind;
using cppgram::PeerRef;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;

namespace {

constexpr std::uint32_t kVectorId = 0x1cb5c415u;
constexpr std::uint32_t kHelpAppConfigId = 0xdd18782eu;
constexpr std::uint32_t kJsonObjectId = 0x99c1d49du;
constexpr std::uint32_t kJsonObjectValueId = 0xc0de1bd9u;
constexpr std::uint32_t kJsonStringId = 0xb71e767au;
constexpr std::uint32_t kSavedMusicNotModifiedId = 0xe3878aa4u;

std::string session_path(const char* tag) {
    static std::atomic<int> counter{0};
    return (std::filesystem::temp_directory_path() /
            ("cppgram-reads-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
             std::to_string(counter++) + ".session"))
        .string();
}

ClientOptions options_for(FakeDc& dc, const std::string& path) {
    ClientOptions o;
    o.api_id = 4242;
    o.api_hash = "00000000000000000000000000000000";  // dummy test value
    o.dc_ip = "127.0.0.1";
    o.dc_port = dc.port();
    o.dc_id = 2;
    o.server_keys = {dc.public_key()};
    o.session_path = path;
    o.connect_timeout_ms = 2000;
    return o;
}

}  // namespace

TEST(AccountReadsFakeDc, AppConfigIsSerializedAsJsonText) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter& out) {
        if (id != fn::help_getAppConfig::kId) return false;
        out.write_uint32(kHelpAppConfigId);
        out.write_int32(42);                     // hash
        out.write_uint32(kJsonObjectId);         // config: jsonObject
        out.write_uint32(kVectorId);
        out.write_int32(1);
        out.write_uint32(kJsonObjectValueId);    // one entry: "x": "ok"
        out.write_string("x");
        out.write_uint32(kJsonStringId);
        out.write_string("ok");
        return true;
    });

    const std::string path = session_path("config");
    Client client(options_for(dc, path));
    client.connect();
    const auto info = client.get_app_config(0);
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_EQ(info.hash, 42);
    EXPECT_FALSE(info.not_modified);
    EXPECT_EQ(info.json, R"({"x":"ok"})");
}

TEST(AccountReadsFakeDc, SavedMusicNotModifiedIsReported) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter& out) {
        if (id != fn::users_getSavedMusic::kId) return false;
        out.write_uint32(kSavedMusicNotModifiedId);
        out.write_int32(7);
        return true;
    });

    const std::string path = session_path("music");
    Client client(options_for(dc, path));
    client.connect();
    PeerRef user;
    user.kind = PeerKind::User;
    user.id = 555;
    user.access_hash = 666;
    user.has_access_hash = true;
    const auto page = client.get_saved_music(user, 0, 100, 99);
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_TRUE(page.not_modified);
    EXPECT_EQ(page.count, 7);
    EXPECT_TRUE(page.documents.empty());
}
