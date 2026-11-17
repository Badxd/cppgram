// Client::edit_story flag wiring and validation against a scripted fake DC. Offline only. NOT RUN in this session.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "cppgram/client.h"
#include "cppgram/mtproto/schema/api.h"
#include "fake_dc.h"

using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::PeerKind;
using cppgram::PeerRef;
using cppgram::StoryEditOptions;
using cppgram::StoryMediaArea;
using cppgram::StoryMediaAreaKind;
using cppgram::StoryPrivacyKind;
using cppgram::StoryPrivacyRule;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;

namespace {

constexpr std::uint32_t kUpdatesTooLongId = 0xe317af7eu;  // updatesTooLong, no body

std::string session_path(const char* tag) {
    static std::atomic<int> counter{0};
    return (std::filesystem::temp_directory_path() /
            ("cppgram-story-edit-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
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

PeerRef user_peer() {
    PeerRef p;
    p.kind = PeerKind::User;
    p.id = 555;
    p.access_hash = 666;
    p.has_access_hash = true;
    return p;
}

}  // namespace

TEST(StoriesEditFakeDc, CaptionMediaAreaAndPrivacySetTheirFlags) {
    FakeDc dc;
    std::uint32_t seen_flags = 0;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader& args, cppgram::mtproto::TlWriter& out) {
        if (id != fn::stories_editStory::kId) return false;
        seen_flags = args.read_uint32();
        out.write_uint32(kUpdatesTooLongId);
        return true;
    });

    const std::string path = session_path("flags");
    Client client(options_for(dc, path));
    client.connect();

    StoryEditOptions options;
    options.caption = "new caption";
    StoryMediaArea url_area;
    url_area.kind = StoryMediaAreaKind::Url;
    url_area.x = 10;
    url_area.y = 20;
    url_area.w = 30;
    url_area.h = 10;
    url_area.url = "https://example.com";
    options.media_areas = std::vector<StoryMediaArea>{url_area};
    StoryPrivacyRule close;
    close.kind = StoryPrivacyKind::CloseFriends;
    options.privacy_rules = std::vector<StoryPrivacyRule>{close};

    EXPECT_TRUE(client.edit_story(user_peer(), 9, options));
    client.disconnect();
    std::filesystem::remove(path);

    // caption = flags.1 (2), privacy_rules = flags.2 (4), media_areas = flags.3 (8)
    EXPECT_EQ(seen_flags, 2u | 4u | 8u);
}

TEST(StoriesEditFakeDc, EmptyOptionsAreRejectedBeforeRpc) {
    FakeDc dc;
    bool rpc_seen = false;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter&) {
        if (id == fn::stories_editStory::kId) rpc_seen = true;
        return false;
    });

    const std::string path = session_path("empty");
    Client client(options_for(dc, path));
    client.connect();
    EXPECT_THROW(client.edit_story(user_peer(), 9, StoryEditOptions{}), std::invalid_argument);
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_FALSE(rpc_seen);
}

TEST(StoriesEditFakeDc, OutOfRangeMediaAreaIsRejectedBeforeRpc) {
    FakeDc dc;
    bool rpc_seen = false;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter&) {
        if (id == fn::stories_editStory::kId) rpc_seen = true;
        return false;
    });

    const std::string path = session_path("area");
    Client client(options_for(dc, path));
    client.connect();
    StoryEditOptions options;
    StoryMediaArea bad;
    bad.kind = StoryMediaAreaKind::Url;
    bad.x = 120;  // percent, must be 0 to 100
    bad.url = "https://example.com";
    options.media_areas = std::vector<StoryMediaArea>{bad};
    EXPECT_THROW(client.edit_story(user_peer(), 9, options), std::invalid_argument);
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_FALSE(rpc_seen);
}

TEST(StoriesEditFakeDc, AllowUsersWithoutAccessHashIsRejected) {
    FakeDc dc;
    const std::string path = session_path("users");
    Client client(options_for(dc, path));
    client.connect();
    StoryEditOptions options;
    StoryPrivacyRule allow;
    allow.kind = StoryPrivacyKind::AllowUsers;
    PeerRef no_hash = user_peer();
    no_hash.has_access_hash = false;
    allow.users = {no_hash};
    options.privacy_rules = std::vector<StoryPrivacyRule>{allow};
    EXPECT_THROW(client.edit_story(user_peer(), 9, options), std::invalid_argument);
    client.disconnect();
    std::filesystem::remove(path);
}
