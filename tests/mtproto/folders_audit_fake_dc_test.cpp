// Folder and chatlist audit regressions against a scripted fake DC. Offline only. NOT RUN in this session.
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
using cppgram::FolderInfo;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;

namespace {

constexpr std::uint32_t kRpcErrorId = 0x2144ca19u;  // rpc_error: error_code int, error_message string

std::string session_path(const char* tag) {
    static std::atomic<int> counter{0};
    return (std::filesystem::temp_directory_path() /
            ("cppgram-folders-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
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

void write_rpc_error(cppgram::mtproto::TlWriter& out) {
    out.write_uint32(kRpcErrorId);
    out.write_int32(400);
    out.write_string("TEST_ERROR");
}

}  // namespace

// A shared (chatlist) folder is owned by the invite API. edit_folder must refuse it before any RPC, because
// messages.updateDialogFilter would overwrite the folder's contents on the server.
TEST(FoldersAuditFakeDc, EditSharedFolderRejectedBeforeRpc) {
    FakeDc dc;
    bool rpc_seen = false;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter&) {
        if (id == fn::messages_updateDialogFilter::kId) rpc_seen = true;
        return false;
    });

    const std::string path = session_path("edit-shared");
    Client client(options_for(dc, path));
    client.connect();
    FolderInfo shared;
    shared.id = 3;
    shared.title = "shared";
    shared.shared = true;
    EXPECT_THROW(client.edit_folder(shared), std::invalid_argument);
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_FALSE(rpc_seen);
}

// Creating a shared folder through create_folder is refused before any RPC. Shared folders come from the invite API.
TEST(FoldersAuditFakeDc, CreateSharedFolderRejectedBeforeRpc) {
    FakeDc dc;
    bool rpc_seen = false;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter&) {
        if (id == fn::messages_updateDialogFilter::kId || id == fn::messages_getDialogFilters::kId) {
            rpc_seen = true;
        }
        return false;
    });

    const std::string path = session_path("create-shared");
    Client client(options_for(dc, path));
    client.connect();
    FolderInfo shared;
    shared.title = "shared";
    shared.shared = true;
    EXPECT_THROW(client.create_folder(shared), std::invalid_argument);
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_FALSE(rpc_seen);
}

// Folder ids outside 2..255 are rejected before any RPC. Id 1 is the reserved default view.
TEST(FoldersAuditFakeDc, EditFolderRejectsReservedIdBeforeRpc) {
    FakeDc dc;
    bool rpc_seen = false;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter&) {
        if (id == fn::messages_updateDialogFilter::kId) rpc_seen = true;
        return false;
    });

    const std::string path = session_path("reserved-id");
    Client client(options_for(dc, path));
    client.connect();
    FolderInfo f;
    f.id = 1;
    f.title = "bad";
    EXPECT_THROW(client.edit_folder(f), std::invalid_argument);
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_FALSE(rpc_seen);
}

// A pasted link with a query string or fragment must send only the slug. Before the fix the request carried
// "abc?x=1#y" and the server returned INVITE_SLUG_EMPTY or a not-found error.
TEST(FoldersAuditFakeDc, CheckInviteStripsQueryAndFragment) {
    FakeDc dc;
    std::string seen_slug;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader& args, cppgram::mtproto::TlWriter& out) {
        if (id != fn::chatlists_checkChatlistInvite::kId) return false;
        seen_slug = args.read_string();
        write_rpc_error(out);  // the test only checks the request
        return true;
    });

    const std::string path = session_path("slug");
    Client client(options_for(dc, path));
    client.connect();
    EXPECT_ANY_THROW(client.check_folder_invite_link("https://t.me/addlist/abc?x=1#y"));
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_EQ(seen_slug, "abc");
}

// A bare slug passes through unchanged.
TEST(FoldersAuditFakeDc, CheckInviteAcceptsBareSlug) {
    FakeDc dc;
    std::string seen_slug;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader& args, cppgram::mtproto::TlWriter& out) {
        if (id != fn::chatlists_checkChatlistInvite::kId) return false;
        seen_slug = args.read_string();
        write_rpc_error(out);
        return true;
    });

    const std::string path = session_path("bare");
    Client client(options_for(dc, path));
    client.connect();
    EXPECT_ANY_THROW(client.check_folder_invite_link("abc"));
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_EQ(seen_slug, "abc");
}

// The chatlist update calls are sent as their own constructors. Each is answered with rpc_error, so the test
// proves emission and that the error reaches the caller. It does not decode a success response.
TEST(FoldersAuditFakeDc, ChatlistUpdateCallsAreEmitted) {
    FakeDc dc;
    std::int32_t seen = 0;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter& out) {
        if (id == fn::chatlists_getChatlistUpdates::kId) seen |= 1;
        else if (id == fn::chatlists_hideChatlistUpdates::kId) seen |= 2;
        else if (id == fn::chatlists_getLeaveChatlistSuggestions::kId) seen |= 4;
        else return false;
        write_rpc_error(out);
        return true;
    });

    const std::string path = session_path("updates");
    Client client(options_for(dc, path));
    client.connect();
    EXPECT_ANY_THROW(client.get_folder_updates(2));
    EXPECT_ANY_THROW(client.hide_folder_updates(2));
    EXPECT_ANY_THROW(client.get_leave_folder_suggestions(2));
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_EQ(seen, 7);
}
