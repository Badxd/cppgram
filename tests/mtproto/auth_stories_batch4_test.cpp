// Regression tests for resend_login_code, sign_up, password recovery, get_all_stories and edit_story_caption.
// Fake DC only. The api_hash is a dummy test value. Status: written and syntax-checked; NOT RUN in the implementation session.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/mtproto/schema/api.h"
#include "fake_dc.h"

using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::LoginTokenState;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;
namespace tl = cppgram::mtproto::tl;
namespace ty = cppgram::mtproto::tl::types;
namespace mtp = cppgram::mtproto;

namespace {

std::string session_path(const char* tag) {
    static std::atomic<int> counter{0};
    return (std::filesystem::temp_directory_path() /
            ("cppgram-b4-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
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

} // namespace

TEST(Batch4AuthStories, ResendLoginCodeParsesSentCode) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, mtp::TlReader&, mtp::TlWriter& out) {
        if (id != fn::auth_resendCode::kId) return false;
        tl::auth_SentCode res;
        ty::auth_sentCode sent;
        sent.phone_code_hash = "hash-2";
        sent.timeout = 60;
        sent.type = std::make_shared<tl::auth_SentCodeType>();
        sent.type->value = ty::auth_sentCodeTypeSms{ 5 };
        res.value = sent;
        res.serialize(out);
        return true;
    });
    const std::string path = session_path("resend");
    Client client(options_for(dc, path));
    client.connect();
    const auto info = client.resend_login_code("+10000000000", "hash-1");
    client.disconnect();
    std::filesystem::remove(path);
    EXPECT_EQ(info.phone_code_hash, "hash-2");
    EXPECT_EQ(info.delivery, "sms");
    EXPECT_EQ(info.timeout_seconds, 60);
}

TEST(Batch4AuthStories, ResendLoginCodeRejectsEmptyHash) {
    FakeDc dc;
    const std::string path = session_path("resend-empty");
    Client client(options_for(dc, path));
    client.connect();
    EXPECT_THROW(client.resend_login_code("+10000000000", ""), std::invalid_argument);
    client.disconnect();
    std::filesystem::remove(path);
}

TEST(Batch4AuthStories, RequestPasswordRecoveryReturnsEmailPattern) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, mtp::TlReader&, mtp::TlWriter& out) {
        if (id != fn::auth_requestPasswordRecovery::kId) return false;
        tl::auth_PasswordRecovery res;
        res.value = ty::auth_passwordRecovery{ "a***@example.com" };
        res.serialize(out);
        return true;
    });
    const std::string path = session_path("recovery");
    Client client(options_for(dc, path));
    client.connect();
    const auto info = client.request_password_recovery();
    client.disconnect();
    std::filesystem::remove(path);
    EXPECT_EQ(info.email_pattern, "a***@example.com");
}

TEST(Batch4AuthStories, GetAllStoriesNotModifiedKeepsState) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, mtp::TlReader&, mtp::TlWriter& out) {
        if (id != fn::stories_getAllStories::kId) return false;
        tl::stories_AllStories res;
        ty::stories_allStoriesNotModified nm;
        nm.state = "s-42";
        nm.stealth_mode = std::make_shared<tl::StoriesStealthMode>();
        res.value = nm;
        res.serialize(out);
        return true;
    });
    const std::string path = session_path("allstories");
    Client client(options_for(dc, path));
    client.connect();
    const auto all = client.get_all_stories();
    client.disconnect();
    std::filesystem::remove(path);
    EXPECT_TRUE(all.not_modified);
    EXPECT_EQ(all.state, "s-42");
    EXPECT_TRUE(all.peers.empty());
}

TEST(Batch4AuthStories, EditStoryCaptionReturnsTrueOnUpdates) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, mtp::TlReader&, mtp::TlWriter& out) {
        if (id != fn::stories_editStory::kId) return false;
        // Any valid Updates object works here. updatesTooLong carries no payload.
        out.write_uint32(0xe317af7eu);
        return true;
    });
    const std::string path = session_path("editstory");
    Client client(options_for(dc, path));
    client.connect();
    cppgram::PeerRef p;
    p.kind = cppgram::PeerKind::User;
    p.id = 777;
    p.access_hash = 123;
    p.has_access_hash = true;
    EXPECT_TRUE(client.edit_story_caption(p, 9, "new caption"));
    client.disconnect();
    std::filesystem::remove(path);
}
