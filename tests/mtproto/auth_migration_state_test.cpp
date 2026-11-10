// Login state across DC migration, on loopback fake DCs (tests/mtproto/fake_dc.h). DC 2 is the home DC. DC 5 issues
// the phone code and holds the account. DC 3 is where an unrelated request (a bot login) is moved to. Each test checks
// which DC received which request. Offline only: fictional numbers, dummy api_hash, no credentials.
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>

#include "cppgram/auth_state.h"
#include "cppgram/client.h"
#include "cppgram/mtproto/session.h"
#include "fake_dc.h"

namespace {

using cppgram::AuthState;
using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::DcAddress;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;
namespace tl = cppgram::mtproto::tl;
namespace ty = cppgram::mtproto::tl::types;
using cppgram::mtproto::TlReader;
using cppgram::mtproto::TlWriter;

constexpr std::uint32_t kRpcErrorId = 0x2144ca19u;  // rpc_error: error_code int, error_message string
constexpr const char* kPhone = "+15550001111";      // fictional number in the reserved 555 range
constexpr const char* kCode = "12345";              // fake login code

// Migrate replies use code 303, everything else 400, as Telegram does.
void write_error(TlWriter& out, const std::string& message) {
    out.write_uint32(kRpcErrorId);
    out.write_int32(message.find("MIGRATE") != std::string::npos ? 303 : 400);
    out.write_string(message);
}

void write_sent_code(TlWriter& out, const std::string& hash) {
    tl::auth_SentCode res;
    ty::auth_sentCode sent;
    sent.phone_code_hash = hash;
    sent.timeout = 60;
    sent.type = std::make_shared<tl::auth_SentCodeType>();
    sent.type->value = ty::auth_sentCodeTypeSms{5};
    res.value = sent;
    res.serialize(out);
}

void write_auth_ok(TlWriter& out) {
    tl::auth_Authorization auth;
    ty::auth_authorization ok;
    auto user = std::make_shared<tl::User>();
    ty::user full;
    full.id = 99887766;
    user->value = full;
    ok.user = user;
    auth.value = ok;
    auth.serialize(out);
}

struct SignInArgs {
    std::string phone;
    std::string hash;
    std::string code;
};

SignInArgs read_sign_in(TlReader& args) {
    SignInArgs s;
    const std::uint32_t flags = args.read_uint32();
    s.phone = args.read_string();
    s.hash = args.read_string();
    if (flags & 1u) s.code = args.read_string();
    return s;
}

std::string temp_session_path(const std::string& name) {
    std::random_device rd;
    const auto dir = std::filesystem::temp_directory_path() / ("cppgram-authmig-" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    return (dir / name).string();
}

// Three fake DCs with switchable behaviour. Every request is counted under "<dc>:<method>".
struct Fleet {
    FakeDc home;  // DC 2
    FakeDc dc5;   // issues codes, holds the account
    FakeDc dc3;   // target of an unrelated migration

    std::mutex mu;
    std::map<std::string, int> calls;
    std::string current_hash = "hash-5";  // the only hash DC 5 accepts for sign-in
    bool home_issues_code = false;        // home answers sendCode itself (hash-2) instead of PHONE_MIGRATE_5
    bool home_sign_in_migrates = false;   // home answers signIn with PHONE_MIGRATE_5
    bool sign_in_needs_password = false;  // DC 5 answers a valid sign-in with SESSION_PASSWORD_NEEDED
    std::string bot_on_dc5 = "USER_MIGRATE_3";  // "" means the bot login is accepted on DC 5
    std::string bot_on_dc3 = "ACCESS_TOKEN_INVALID";

    Fleet() {
        home.set_handler([this](std::uint32_t id, TlReader& args, TlWriter& out) { return on_home(id, args, out); });
        dc5.set_handler([this](std::uint32_t id, TlReader& args, TlWriter& out) { return on_dc5(id, args, out); });
        dc3.set_handler([this](std::uint32_t id, TlReader& args, TlWriter& out) { return on_dc3(id, args, out); });
    }

    int count(const std::string& key) {
        std::lock_guard<std::mutex> lock(mu);
        const auto it = calls.find(key);
        return it == calls.end() ? 0 : it->second;
    }

    bool on_home(std::uint32_t id, TlReader&, TlWriter& out) {
        std::lock_guard<std::mutex> lock(mu);
        if (id == fn::auth_sendCode::kId) {
            ++calls["2:sendCode"];
            if (home_issues_code) {
                write_sent_code(out, "hash-2");
            } else {
                write_error(out, "PHONE_MIGRATE_5");
            }
            return true;
        }
        if (id == fn::auth_signIn::kId) {
            ++calls["2:signIn"];
            write_error(out, home_sign_in_migrates ? "PHONE_MIGRATE_5" : "PHONE_CODE_INVALID");
            return true;
        }
        if (id == fn::account_getPassword::kId) {
            ++calls["2:getPassword"];
            write_error(out, "PASSWORD_HASH_INVALID");
            return true;
        }
        if (id == fn::auth_importBotAuthorization::kId) {
            ++calls["2:importBot"];
            write_error(out, "USER_MIGRATE_5");
            return true;
        }
        return false;
    }

    bool on_dc5(std::uint32_t id, TlReader& args, TlWriter& out) {
        std::lock_guard<std::mutex> lock(mu);
        if (id == fn::auth_sendCode::kId) {
            ++calls["5:sendCode"];
            current_hash = "hash-5";
            write_sent_code(out, current_hash);
            return true;
        }
        if (id == fn::auth_resendCode::kId) {
            ++calls["5:resendCode"];
            args.read_uint32();  // flags
            args.read_string();  // phone number
            args.read_string();  // phone_code_hash the client sent
            current_hash = "hash-5b";
            write_sent_code(out, current_hash);
            return true;
        }
        if (id == fn::auth_signIn::kId) {
            ++calls["5:signIn"];
            const SignInArgs s = read_sign_in(args);
            if (s.hash != current_hash || s.code != kCode) {
                write_error(out, "PHONE_CODE_INVALID");
            } else if (sign_in_needs_password) {
                // Not an RPC error in the usual sense, but the fake sends it as one, as Telegram does.
                out.write_uint32(kRpcErrorId);
                out.write_int32(401);
                out.write_string("SESSION_PASSWORD_NEEDED");
            } else {
                write_auth_ok(out);
            }
            return true;
        }
        if (id == fn::account_getPassword::kId) {
            ++calls["5:getPassword"];
            write_error(out, "PASSWORD_HASH_INVALID");
            return true;
        }
        if (id == fn::auth_importBotAuthorization::kId) {
            ++calls["5:importBot"];
            if (bot_on_dc5.empty()) {
                write_auth_ok(out);
            } else {
                write_error(out, bot_on_dc5);
            }
            return true;
        }
        return false;
    }

    bool on_dc3(std::uint32_t id, TlReader&, TlWriter& out) {
        std::lock_guard<std::mutex> lock(mu);
        if (id == fn::auth_importBotAuthorization::kId) {
            ++calls["3:importBot"];
            if (bot_on_dc3.empty()) {
                write_auth_ok(out);
            } else {
                write_error(out, bot_on_dc3);
            }
            return true;
        }
        if (id == fn::auth_sendCode::kId) {
            ++calls["3:sendCode"];
        } else if (id == fn::auth_resendCode::kId) {
            ++calls["3:resendCode"];
        } else if (id == fn::auth_signIn::kId) {
            ++calls["3:signIn"];
        } else if (id == fn::account_getPassword::kId) {
            ++calls["3:getPassword"];
        } else {
            return false;
        }
        write_error(out, "ACCESS_TOKEN_INVALID");
        return true;
    }
};

ClientOptions options_for(Fleet& f, const std::string& session_path) {
    ClientOptions o;
    o.api_id = 1;
    o.api_hash = "00000000000000000000000000000000";  // dummy test value
    o.dc_ip = "127.0.0.1";
    o.dc_port = f.home.port();
    o.dc_id = 2;
    o.server_keys = {f.home.public_key(), f.dc5.public_key(), f.dc3.public_key()};
    o.dc_addresses = {DcAddress{5, "127.0.0.1", f.dc5.port()}, DcAddress{3, "127.0.0.1", f.dc3.port()}};
    o.session_path = session_path;
    o.connect_timeout_ms = 2000;
    o.retry.max_attempts = 2;
    o.retry.base = std::chrono::milliseconds(10);
    o.retry.cap = std::chrono::milliseconds(50);
    return o;
}

}  // namespace

// The code is issued by DC 5. An unrelated bot login is moved to DC 3 and fails there. The pending code must survive,
// and sign-in must go back to DC 5 and succeed there. DC 3 must not see the sign-in.
TEST(AuthMigrationState, PhoneCodeSurvivesUnrelatedMigrationAndSignsInOnTheIssuingDc) {
    Fleet f;
    Client client(options_for(f, temp_session_path("unrelated.session")));
    client.connect();
    client.send_login_code(kPhone);
    ASSERT_EQ(client.auth_state(), AuthState::CodeRequired);

    EXPECT_THROW(client.login_bot("123456:TEST_TOKEN_NOT_REAL"), cppgram::mtproto::RpcError);
    EXPECT_EQ(f.count("3:importBot"), 1) << "the bot login must have been moved to DC 3";
    EXPECT_EQ(client.auth_state(), AuthState::CodeRequired) << "the unrelated migration must keep the pending code";

    bool ok = false;
    ASSERT_NO_THROW(ok = client.sign_in_code(kPhone, kCode, "hash-5"));
    EXPECT_TRUE(ok);
    EXPECT_EQ(client.auth_state(), AuthState::Authorized);
    EXPECT_EQ(f.count("5:signIn"), 1);
    EXPECT_EQ(f.count("3:signIn"), 0);
    EXPECT_EQ(f.count("3:sendCode"), 0);
    client.disconnect();
}

// Bot login migration without a pending phone login is unchanged: the home DC moves the bot to DC 5 and the login
// is accepted there.
TEST(AuthMigrationState, BotLoginMigrationStillWorksWithoutAPendingCode) {
    Fleet f;
    f.bot_on_dc5 = "";
    Client client(options_for(f, temp_session_path("bot.session")));
    client.connect();
    ASSERT_NO_THROW(client.login_bot("123456:TEST_TOKEN_NOT_REAL"));
    EXPECT_EQ(f.count("2:importBot"), 1);
    EXPECT_EQ(f.count("5:importBot"), 1);
    EXPECT_EQ(client.auth_state(), AuthState::Authorized);
    client.disconnect();
}

// A bot login that succeeds ends a pending phone login. The session now belongs to the bot account.
TEST(AuthMigrationState, BotLoginSuccessEndsAPendingPhoneLogin) {
    Fleet f;
    f.bot_on_dc5 = "";
    Client client(options_for(f, temp_session_path("botends.session")));
    client.connect();
    client.send_login_code(kPhone);
    ASSERT_EQ(client.auth_state(), AuthState::CodeRequired);
    ASSERT_NO_THROW(client.login_bot("123456:TEST_TOKEN_NOT_REAL"));
    EXPECT_EQ(client.auth_state(), AuthState::Authorized);
    EXPECT_THROW(client.sign_in_code(kPhone, kCode, "hash-5"), cppgram::errors::AuthenticationError);
    EXPECT_EQ(f.count("5:signIn"), 0);
    client.disconnect();
}

// The home DC answers signIn with PHONE_MIGRATE_5. The code was issued by home, so it is not sent to DC 5. The
// pending code is dropped, and a new code request then works.
TEST(AuthMigrationState, PhoneMigrationDuringSignInDropsTheCodeAndDoesNotSendItElsewhere) {
    Fleet f;
    f.home_issues_code = true;
    f.home_sign_in_migrates = true;
    Client client(options_for(f, temp_session_path("signinmig.session")));
    client.connect();
    client.send_login_code(kPhone);
    ASSERT_EQ(client.auth_state(), AuthState::CodeRequired);

    EXPECT_THROW(client.sign_in_code(kPhone, kCode, "hash-2"), cppgram::errors::AuthenticationError);
    EXPECT_EQ(f.count("2:signIn"), 1);
    EXPECT_EQ(f.count("5:signIn"), 0) << "a code issued by one DC must not be sent to another";
    EXPECT_NE(client.auth_state(), AuthState::CodeRequired);

    f.home_issues_code = false;
    f.home_sign_in_migrates = false;
    client.send_login_code(kPhone);
    EXPECT_EQ(f.count("5:sendCode"), 1) << "the new request must go to the account DC";
    bool ok = false;
    ASSERT_NO_THROW(ok = client.sign_in_code(kPhone, kCode, "hash-5"));
    EXPECT_TRUE(ok);
    EXPECT_EQ(f.count("5:signIn"), 1);
    client.disconnect();
}

// 2FA after an unrelated migration: the password step goes back to DC 5, where the sign-in was accepted.
TEST(AuthMigrationState, PasswordRequiredSurvivesUnrelatedMigration) {
    Fleet f;
    f.sign_in_needs_password = true;
    Client client(options_for(f, temp_session_path("pw.session")));
    client.connect();
    client.send_login_code(kPhone);
    EXPECT_FALSE(client.sign_in_code(kPhone, kCode, "hash-5"));
    ASSERT_EQ(client.auth_state(), AuthState::PasswordRequired);

    EXPECT_THROW(client.login_bot("123456:TEST_TOKEN_NOT_REAL"), cppgram::mtproto::RpcError);
    EXPECT_EQ(client.auth_state(), AuthState::PasswordRequired);

    EXPECT_THROW(client.sign_in_password("not-the-password"), cppgram::mtproto::RpcError);
    EXPECT_EQ(f.count("5:getPassword"), 1) << "the 2FA step must go back to DC 5";
    EXPECT_EQ(f.count("3:getPassword"), 0);
    EXPECT_EQ(client.auth_state(), AuthState::PasswordRequired);
    client.disconnect();
}

// Resend after an unrelated migration runs on DC 5, the DC that issued the pending code. The new hash is the one
// DC 5 accepts. The old hash is refused.
TEST(AuthMigrationState, ResendAfterUnrelatedMigrationUsesTheIssuingDc) {
    Fleet f;
    Client client(options_for(f, temp_session_path("resend.session")));
    client.connect();
    client.send_login_code(kPhone);
    EXPECT_THROW(client.login_bot("123456:TEST_TOKEN_NOT_REAL"), cppgram::mtproto::RpcError);
    ASSERT_EQ(f.count("3:importBot"), 1);

    const auto info = client.resend_login_code(kPhone, "hash-5");
    EXPECT_EQ(info.phone_code_hash, "hash-5b");
    EXPECT_EQ(f.count("5:resendCode"), 1);
    EXPECT_EQ(f.count("3:resendCode"), 0);
    EXPECT_EQ(client.auth_state(), AuthState::CodeRequired);

    EXPECT_THROW(client.sign_in_code(kPhone, kCode, "hash-5"), cppgram::mtproto::RpcError)
        << "the hash from before the resend must no longer be accepted";
    bool ok = false;
    ASSERT_NO_THROW(ok = client.sign_in_code(kPhone, kCode, "hash-5b"));
    EXPECT_TRUE(ok);
    EXPECT_EQ(f.count("5:signIn"), 2);
    client.disconnect();
}

// A migration that fails on the connect leaves a consistent state. Connection down, no pending code, previous DC
// settings back, the saved key untouched. A fresh connect() dials the saved DC 5 and a new code works.
TEST(AuthMigrationState, FailedMigrationLeavesAConsistentStateAndRecovers) {
    Fleet f;
    const std::string path = temp_session_path("failed.session");
    Client client(options_for(f, path));
    client.connect();
    client.send_login_code(kPhone);
    cppgram::mtproto::SessionData before;
    ASSERT_TRUE(cppgram::mtproto::load_session_file(path, before));
    ASSERT_EQ(before.dc_id, 5);

    f.dc3.set_refusing(true);
    EXPECT_ANY_THROW(client.login_bot("123456:TEST_TOKEN_NOT_REAL"));
    EXPECT_FALSE(client.is_connected());
    EXPECT_EQ(client.auth_state(), AuthState::Disconnected);
    EXPECT_THROW(client.sign_in_code(kPhone, kCode, "hash-5"), std::runtime_error)
        << "a pending code must not survive a failed migration (the client is disconnected)";

    cppgram::mtproto::SessionData after;
    ASSERT_TRUE(cppgram::mtproto::load_session_file(path, after));
    EXPECT_EQ(after.dc_id, 5);
    EXPECT_EQ(after.auth_key, before.auth_key) << "a failed migration must not replace the saved key";

    // With no saved session, connect() has nothing to route by. It must use the DC settings restored after the failure.
    std::filesystem::remove(path);
    const int dc5_before = f.dc5.connections_accepted();
    ASSERT_NO_THROW(client.connect());
    EXPECT_GT(f.dc5.connections_accepted(), dc5_before) << "connect() must dial DC 5, the DC before the failed move";
    EXPECT_EQ(client.auth_state(), AuthState::Unauthenticated);
    f.dc3.set_refusing(false);
    client.send_login_code(kPhone);
    bool ok = false;
    ASSERT_NO_THROW(ok = client.sign_in_code(kPhone, kCode, "hash-5"));
    EXPECT_TRUE(ok);
    client.disconnect();
}

// Bounded: the bot login is answered with USER_MIGRATE_3 on DC 5 and USER_MIGRATE_5 on DC 3. It stops after two
// hops (three requests), and the error is returned. The pending code is kept and sign-in still works on DC 5.
TEST(AuthMigrationState, RepeatedMigrationStopsAndKeepsThePendingCode) {
    Fleet f;
    f.bot_on_dc3 = "USER_MIGRATE_5";
    Client client(options_for(f, temp_session_path("repeat.session")));
    client.connect();
    client.send_login_code(kPhone);
    EXPECT_THROW(client.login_bot("123456:TEST_TOKEN_NOT_REAL"), cppgram::mtproto::RpcError);
    EXPECT_EQ(f.count("5:importBot"), 2);
    EXPECT_EQ(f.count("3:importBot"), 1);
    EXPECT_EQ(client.auth_state(), AuthState::CodeRequired);

    bool ok = false;
    ASSERT_NO_THROW(ok = client.sign_in_code(kPhone, kCode, "hash-5"));
    EXPECT_TRUE(ok);
    EXPECT_EQ(f.count("5:signIn"), 1);
    client.disconnect();
}

// An explicit disconnect ends the login. After connect() the old code is not reused, even though its hash is known.
TEST(AuthMigrationState, ExplicitDisconnectThenFreshLoginDropsTheOldCode) {
    Fleet f;
    Client client(options_for(f, temp_session_path("fresh.session")));
    client.connect();
    client.send_login_code(kPhone);
    ASSERT_EQ(client.auth_state(), AuthState::CodeRequired);

    client.disconnect();
    EXPECT_EQ(client.auth_state(), AuthState::Disconnected);
    EXPECT_THROW(client.sign_in_code(kPhone, kCode, "hash-5"), std::runtime_error);  // not connected

    client.connect();
    EXPECT_EQ(client.auth_state(), AuthState::Unauthenticated);
    EXPECT_THROW(client.sign_in_code(kPhone, kCode, "hash-5"), cppgram::errors::AuthenticationError)
        << "a fresh connect() must not reuse the old code";
    EXPECT_EQ(f.count("5:signIn"), 0);

    client.send_login_code(kPhone);
    bool ok = false;
    ASSERT_NO_THROW(ok = client.sign_in_code(kPhone, kCode, "hash-5"));
    EXPECT_TRUE(ok);
    client.disconnect();
}
