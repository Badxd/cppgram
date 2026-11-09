// Story read and view methods against a scripted fake DC. Offline only. The api_hash is a dummy test value.
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
#include "fake_dc.h"

using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;
namespace tl = cppgram::mtproto::tl;
namespace ty = cppgram::mtproto::tl::types;
namespace mtp = cppgram::mtproto;

namespace {

std::string session_path(const char* tag) {
    static std::atomic<int> counter{0};
    return (std::filesystem::temp_directory_path() /
            ("cppgram-stories-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
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

// Client methods take a peer. A user peer is enough: the fake DC does not check it.
cppgram::PeerRef some_user() {
    cppgram::PeerRef p;
    p.kind = cppgram::PeerKind::User;
    p.id = 777;
    p.access_hash = 123;
    p.has_access_hash = true;
    return p;
}

} // namespace

TEST(StoriesFakeDc, CanSendStoryCountReturnsRemainingCount) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, mtp::TlReader&, mtp::TlWriter& out) {
        if (id != fn::stories_canSendStory::kId) return false;
        tl::stories_CanSendStoryCount res;
        res.value = ty::stories_canSendStoryCount{ 3 };
        res.serialize(out);
        return true;
    });
    const std::string path = session_path("cansend");
    Client client(options_for(dc, path));
    client.connect();
    EXPECT_EQ(client.can_send_story_count(some_user()), 3);
    client.disconnect();
    std::filesystem::remove(path);
}

TEST(StoriesFakeDc, IncrementStoryViewsReturnsServerBool) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, mtp::TlReader&, mtp::TlWriter& out) {
        if (id != fn::stories_incrementStoryViews::kId) return false;
        out.write_uint32(0x997275b5u);  // boolTrue
        return true;
    });
    const std::string path = session_path("incr");
    Client client(options_for(dc, path));
    client.connect();
    EXPECT_TRUE(client.increment_story_views(some_user(), { 1, 2 }));
    client.disconnect();
    std::filesystem::remove(path);
}

TEST(StoriesFakeDc, IncrementStoryViewsRejectsEmptyList) {
    FakeDc dc;
    const std::string path = session_path("incr-empty");
    Client client(options_for(dc, path));
    client.connect();
    EXPECT_THROW(client.increment_story_views(some_user(), {}), std::invalid_argument);
    client.disconnect();
    std::filesystem::remove(path);
}

TEST(StoriesFakeDc, GetPeerStoriesSkipsDeletedEntries) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, mtp::TlReader&, mtp::TlWriter& out) {
        if (id != fn::stories_getPeerStories::kId) return false;
        auto deleted = std::make_shared<tl::StoryItem>();
        deleted->value = ty::storyItemDeleted{ 5 };
        ty::peerStories peer_stories;
        peer_stories.peer = std::make_shared<tl::Peer>();
        peer_stories.peer->value = ty::peerUser{ 777 };
        peer_stories.stories = { deleted };
        auto ps = std::make_shared<tl::PeerStories>();
        ps->value = peer_stories;
        tl::stories_PeerStories res;
        res.value = ty::stories_peerStories{ ps, {}, {} };
        res.serialize(out);
        return true;
    });
    const std::string path = session_path("peer");
    Client client(options_for(dc, path));
    client.connect();
    EXPECT_TRUE(client.get_peer_stories(some_user()).empty());
    client.disconnect();
    std::filesystem::remove(path);
}

TEST(StoriesFakeDc, GetStoryViewsReturnsViewersAndNextOffset) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, mtp::TlReader&, mtp::TlWriter& out) {
        if (id != fn::stories_getStoryViewsList::kId) return false;
        auto v1 = std::make_shared<tl::StoryView>();
        v1->value = ty::storyView{ false, false, 111, 1700000100, std::nullopt };
        auto v2 = std::make_shared<tl::StoryView>();
        v2->value = ty::storyView{ true, false, 222, 1700000200, std::nullopt };
        tl::stories_StoryViewsList res;
        ty::stories_storyViewsList list;
        list.count = 2;
        list.views_count = 2;
        list.forwards_count = 0;
        list.reactions_count = 0;
        list.views = { v1, v2 };
        list.next_offset = std::string("next-page");
        res.value = list;
        res.serialize(out);
        return true;
    });
    const std::string path = session_path("views");
    Client client(options_for(dc, path));
    client.connect();
    const auto page = client.get_story_views(some_user(), 9, "", 2);
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_EQ(page.views_count, 2);
    EXPECT_EQ(page.next_offset, "next-page");
    ASSERT_EQ(page.views.size(), 2u);
    EXPECT_EQ(page.views[0].user_id, 111);
    EXPECT_EQ(page.views[0].date, 1700000100);
    EXPECT_FALSE(page.views[0].blocked);
    EXPECT_EQ(page.views[1].user_id, 222);
    EXPECT_TRUE(page.views[1].blocked);
}

TEST(StoriesFakeDc, GetStoryViewsRejectsNonPositiveLimit) {
    FakeDc dc;
    const std::string path = session_path("views-limit");
    Client client(options_for(dc, path));
    client.connect();
    EXPECT_THROW(client.get_story_views(some_user(), 9, "", 0), std::invalid_argument);
    client.disconnect();
    std::filesystem::remove(path);
}
