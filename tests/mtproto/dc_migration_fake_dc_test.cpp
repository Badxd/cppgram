// DC migration against loopback fake DCs (tests/mtproto/fake_dc.h). Covers a bot login that is answered with
// USER_MIGRATE_N on the wrong DC, a restored session that belongs to another DC, a bounded migration loop, and an
// unknown home DC. Offline only: no Telegram servers and no credentials are used. The api_hash is a dummy value.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>

#include "cppgram/client.h"
#include "cppgram/dc.h"
#include "cppgram/mtproto/session.h"
#include "fake_dc.h"

namespace {

using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::DcAddress;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;
namespace tl = cppgram::mtproto::tl;
using cppgram::mtproto::TlReader;
using cppgram::mtproto::TlWriter;

constexpr std::uint32_t kRpcErrorId = 0x2144ca19u;  // rpc_error: error_code int, error_message string

void write_rpc_error(TlWriter& out, std::int32_t code, const std::string& message) {
    out.write_uint32(kRpcErrorId);
    out.write_int32(code);
    out.write_string(message);
}

void write_auth_ok(TlWriter& out) {
    tl::auth_Authorization auth;
    tl::types::auth_authorization ok;
    auto user = std::make_shared<tl::User>();
    tl::types::user full;
    full.id = 99887766;
    user->value = full;
    ok.user = user;
    auth.value = ok;
    auth.serialize(out);
}

std::string temp_session_path(const std::string& name) {
    std::random_device rd;
    const auto dir = std::filesystem::temp_directory_path() / ("cppgram-dcmig-" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    return (dir / name).string();
}

ClientOptions options_for(FakeDc& home, FakeDc& other, const std::string& session_path) {
    ClientOptions o;
    o.api_id = 1;
    o.api_hash = "00000000000000000000000000000000";  // dummy test value
    o.dc_ip = "127.0.0.1";
    o.dc_port = home.port();
    o.dc_id = 2;
    o.server_keys = {home.public_key(), other.public_key()};
    o.dc_addresses = {DcAddress{5, "127.0.0.1", other.port()}};
    o.session_path = session_path;
    o.connect_timeout_ms = 2000;
    o.retry.max_attempts = 2;
    o.retry.base = std::chrono::milliseconds(10);
    o.retry.cap = std::chrono::milliseconds(50);
    return o;
}

}  // namespace

TEST(DcMigrationParse, ReadsDcNumberFromMigrateErrorsOnly) {
    EXPECT_EQ(cppgram::migrate_dc_from_error("USER_MIGRATE_5"), 5);
    EXPECT_EQ(cppgram::migrate_dc_from_error("PHONE_MIGRATE_2"), 2);
    EXPECT_EQ(cppgram::migrate_dc_from_error("NETWORK_MIGRATE_4"), 4);
    EXPECT_EQ(cppgram::migrate_dc_from_error("USER_MIGRATE_99"), 99);
    EXPECT_EQ(cppgram::migrate_dc_from_error("USER_MIGRATE_100"), 0);  // not a DC number
    EXPECT_EQ(cppgram::migrate_dc_from_error("FLOOD_WAIT_3"), 0);
    EXPECT_EQ(cppgram::migrate_dc_from_error("PHONE_CODE_INVALID"), 0);
}

// A bot whose home DC is 5 gets USER_MIGRATE_5 from DC 2. The client must move to DC 5, repeat the login there,
// and save the key for DC 5. Before the fix the error reached the caller and nothing moved.
TEST(DcMigrationFakeDc, BotLoginFollowsUserMigrateToHomeDc) {
    FakeDc dc2;
    FakeDc dc5;
    std::atomic<int> asked_dc2{0};
    std::atomic<int> asked_dc5{0};
    dc2.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::auth_importBotAuthorization::kId) return false;
        ++asked_dc2;
        write_rpc_error(out, 303, "USER_MIGRATE_5");
        return true;
    });
    dc5.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::auth_importBotAuthorization::kId) return false;
        ++asked_dc5;
        write_auth_ok(out);
        return true;
    });

    const std::string path = temp_session_path("migrate.session");
    {
        Client client(options_for(dc2, dc5, path));
        client.connect();
        ASSERT_NO_THROW(client.login_bot("123456:TEST_TOKEN_NOT_REAL"));
        client.disconnect();
    }
    EXPECT_EQ(asked_dc2.load(), 1);
    EXPECT_EQ(asked_dc5.load(), 1);
    cppgram::mtproto::SessionData saved;
    ASSERT_TRUE(cppgram::mtproto::load_session_file(path, saved));
    EXPECT_EQ(saved.dc_id, 5);
    EXPECT_EQ(saved.auth_key.size(), 256u);
}

// After migrating, a restart must dial the saved DC. Here the options name DC 2, but the saved key is for DC 5.
TEST(DcMigrationFakeDc, RestartDialsTheDcOfTheSavedSession) {
    FakeDc dc2;
    FakeDc dc5;
    dc2.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::auth_importBotAuthorization::kId) return false;
        write_rpc_error(out, 303, "USER_MIGRATE_5");
        return true;
    });
    dc5.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::auth_importBotAuthorization::kId) return false;
        write_auth_ok(out);
        return true;
    });

    const std::string path = temp_session_path("restart.session");
    {
        Client first(options_for(dc2, dc5, path));
        first.connect();
        first.login_bot("123456:TEST_TOKEN_NOT_REAL");
        first.disconnect();
    }
    const int dc2_before = dc2.connections_accepted();
    const int dc5_before = dc5.connections_accepted();
    {
        Client second(options_for(dc2, dc5, path));
        ASSERT_NO_THROW(second.connect());
        second.disconnect();
    }
    EXPECT_EQ(dc2.connections_accepted(), dc2_before) << "a saved DC 5 session must not be dialled on DC 2";
    EXPECT_GT(dc5.connections_accepted(), dc5_before);
    cppgram::mtproto::SessionData saved;
    ASSERT_TRUE(cppgram::mtproto::load_session_file(path, saved));
    EXPECT_EQ(saved.dc_id, 5);
}

// A DC that keeps answering with a migrate error must not cause an endless loop. Two hops are allowed, so the
// login is repeated three times in total and then the error is returned.
TEST(DcMigrationFakeDc, MigrationLoopIsBounded) {
    FakeDc dc2;
    FakeDc dc5;
    std::atomic<int> asked_dc2{0};
    std::atomic<int> asked_dc5{0};
    dc2.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::auth_importBotAuthorization::kId) return false;
        ++asked_dc2;
        write_rpc_error(out, 303, "USER_MIGRATE_5");
        return true;
    });
    dc5.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::auth_importBotAuthorization::kId) return false;
        ++asked_dc5;
        write_rpc_error(out, 303, "USER_MIGRATE_5");
        return true;
    });

    Client client(options_for(dc2, dc5, temp_session_path("loop.session")));
    client.connect();
    try {
        client.login_bot("123456:TEST_TOKEN_NOT_REAL");
        FAIL() << "login_bot must not succeed while every DC migrates";
    } catch (const cppgram::mtproto::RpcError& e) {
        EXPECT_EQ(e.code(), 303);
        EXPECT_EQ(e.rpc_message(), "USER_MIGRATE_5");
    }
    EXPECT_EQ(asked_dc2.load(), 1);
    EXPECT_EQ(asked_dc5.load(), 2);
    client.disconnect();
}

// A migrate error for a DC with no known address must fail cleanly, without a connection to a made-up address.
TEST(DcMigrationFakeDc, UnknownHomeDcFailsWithoutRetry) {
    FakeDc dc2;
    FakeDc dc5;
    std::atomic<int> asked_dc2{0};
    dc2.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::auth_importBotAuthorization::kId) return false;
        ++asked_dc2;
        write_rpc_error(out, 303, "USER_MIGRATE_9");
        return true;
    });

    Client client(options_for(dc2, dc5, temp_session_path("unknown.session")));
    client.connect();
    EXPECT_THROW(client.login_bot("123456:TEST_TOKEN_NOT_REAL"), std::runtime_error);
    EXPECT_EQ(asked_dc2.load(), 1);
    client.disconnect();
}
