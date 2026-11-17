// Phone-number login against loopback fake DCs (tests/mtproto/fake_dc.h): auth.sendCode and auth.signIn answered
// with PHONE_MIGRATE_N by the home DC, the code request repeated on DC N, the phone_code_hash used on the DC that
// issued it, the 2FA step after migration, bounded retries, and a connection failure during migration. Offline only.
// No phone number, Telegram account or credential is used: the numbers are fictional and the api_hash is a dummy.
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
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
constexpr const char* kCode = "12345";              // fake login code, accepted only by the fake DC 5

void write_rpc_error(TlWriter& out, std::int32_t code, const std::string& message) {
    out.write_uint32(kRpcErrorId);
    out.write_int32(code);
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

// What a signIn request carried, read from the wire. flags bit 0 means phone_code is present.
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
    const auto dir = std::filesystem::temp_directory_path() / ("cppgram-phonemig-" + std::to_string(rd()));
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

// Home DC (2) says the number belongs to DC 5. DC 5 issues the code and accepts the matching sign-in.
struct PhoneDcs {
    FakeDc home;
    FakeDc dc5;
    std::atomic<int> home_send{0};
    std::atomic<int> dc5_send{0};
    std::atomic<int> home_sign_in{0};
    std::atomic<int> dc5_sign_in{0};
    std::atomic<int> dc5_get_password{0};
    std::atomic<int> home_get_password{0};
    std::mutex mu;
    std::string dc5_phone;     // phone number that reached DC 5 in auth.sendCode
    std::string dc5_hash;      // hash that reached DC 5 in auth.signIn
    bool sign_in_needs_password = false;

    PhoneDcs() {
        home.set_handler([this](std::uint32_t id, TlReader&, TlWriter& out) {
            if (id == fn::auth_sendCode::kId) {
                ++home_send;
                write_rpc_error(out, 303, "PHONE_MIGRATE_5");
                return true;
            }
            if (id == fn::auth_signIn::kId) {
                ++home_sign_in;
                write_rpc_error(out, 400, "PHONE_CODE_INVALID");
                return true;
            }
            if (id == fn::account_getPassword::kId) {
                ++home_get_password;
                write_rpc_error(out, 400, "PASSWORD_HASH_INVALID");
                return true;
            }
            return false;
        });
        dc5.set_handler([this](std::uint32_t id, TlReader& args, TlWriter& out) {
            if (id == fn::auth_sendCode::kId) {
                ++dc5_send;
                std::lock_guard<std::mutex> lock(mu);
                dc5_phone = args.read_string();
                write_sent_code(out, "hash-5");
                return true;
            }
            if (id == fn::auth_signIn::kId) {
                ++dc5_sign_in;
                const SignInArgs s = read_sign_in(args);
                std::lock_guard<std::mutex> lock(mu);
                dc5_hash = s.hash;
                if (s.hash != "hash-5" || s.code != kCode) {
                    write_rpc_error(out, 400, "PHONE_CODE_INVALID");
                } else if (sign_in_needs_password) {
                    write_rpc_error(out, 401, "SESSION_PASSWORD_NEEDED");
                } else {
                    write_auth_ok(out);
                }
                return true;
            }
            if (id == fn::account_getPassword::kId) {
                ++dc5_get_password;
                write_rpc_error(out, 400, "PASSWORD_HASH_INVALID");
                return true;
            }
            return false;
        });
    }
};

}  // namespace

// The first code request is answered with PHONE_MIGRATE_5. The same request must be repeated on DC 5, and the hash
// that DC 5 returns must be stored for the sign-in. The state must be CodeRequired afterwards.
TEST(PhoneMigrationFakeDc, SendCodeFollowsPhoneMigrateAndRetriesOnTheNewDc) {
    PhoneDcs dcs;
    const std::string path = temp_session_path("sendcode.session");
    Client client(options_for(dcs.home, dcs.dc5, path));
    client.connect();
    const auto info = client.send_login_code(kPhone);
    EXPECT_EQ(info.phone_code_hash, "hash-5");
    EXPECT_EQ(client.auth_state(), AuthState::CodeRequired);
    EXPECT_EQ(dcs.home_send.load(), 1);
    EXPECT_EQ(dcs.dc5_send.load(), 1);
    {
        std::lock_guard<std::mutex> lock(dcs.mu);
        EXPECT_EQ(dcs.dc5_phone, kPhone) << "the retry must carry the same phone number";
    }
    client.disconnect();

    cppgram::mtproto::SessionData saved;
    ASSERT_TRUE(cppgram::mtproto::load_session_file(path, saved));
    EXPECT_EQ(saved.dc_id, 5) << "the session must now belong to DC 5";
}

// After a migrated code request, sign-in must use the hash from DC 5, and must go to DC 5 only.
TEST(PhoneMigrationFakeDc, SignInUsesTheHashFromTheMigratedDc) {
    PhoneDcs dcs;
    Client client(options_for(dcs.home, dcs.dc5, temp_session_path("signin.session")));
    client.connect();
    client.send_login_code(kPhone);
    ASSERT_TRUE(client.sign_in_code(kPhone, kCode, "hash-5"));
    EXPECT_EQ(client.auth_state(), AuthState::Authorized);
    EXPECT_EQ(dcs.dc5_sign_in.load(), 1);
    EXPECT_EQ(dcs.home_sign_in.load(), 0) << "sign-in must not be sent to the DC that refused the phone number";
    {
        std::lock_guard<std::mutex> lock(dcs.mu);
        EXPECT_EQ(dcs.dc5_hash, "hash-5");
    }
    client.disconnect();
}

// A hash that DC 5 did not issue is refused by DC 5. The client must not report a sign-in.
TEST(PhoneMigrationFakeDc, SignInWithAHashFromAnotherDcIsRefused) {
    PhoneDcs dcs;
    Client client(options_for(dcs.home, dcs.dc5, temp_session_path("wronghash.session")));
    client.connect();
    client.send_login_code(kPhone);
    try {
        client.sign_in_code(kPhone, kCode, "hash-2");
        FAIL() << "a hash from another DC must not sign in";
    } catch (const cppgram::mtproto::RpcError& e) {
        EXPECT_EQ(e.rpc_message(), "PHONE_CODE_INVALID");
    }
    EXPECT_NE(client.auth_state(), AuthState::Authorized);
    client.disconnect();
}

// The account has 2FA. The step after sign-in must stay on DC 5, and the state must remain PasswordRequired.
TEST(PhoneMigrationFakeDc, PasswordRequiredAfterMigrationStaysOnTheNewDc) {
    PhoneDcs dcs;
    dcs.sign_in_needs_password = true;
    Client client(options_for(dcs.home, dcs.dc5, temp_session_path("password.session")));
    client.connect();
    client.send_login_code(kPhone);
    EXPECT_FALSE(client.sign_in_code(kPhone, kCode, "hash-5"));
    EXPECT_EQ(client.auth_state(), AuthState::PasswordRequired);

    // The fake answers the password settings request with a wrong-password error. That is enough to show the
    // step reached DC 5 and that a wrong password keeps the state at PasswordRequired.
    try {
        client.sign_in_password("not-the-password");
        FAIL() << "the fake DC rejects every password";
    } catch (const cppgram::mtproto::RpcError& e) {
        EXPECT_EQ(e.rpc_message(), "PASSWORD_HASH_INVALID");
    }
    EXPECT_EQ(client.auth_state(), AuthState::PasswordRequired);
    EXPECT_EQ(dcs.dc5_get_password.load(), 1);
    EXPECT_EQ(dcs.home_get_password.load(), 0);
    client.disconnect();
}

// A migration to a DC with no known address must fail clearly, without another request and without a connection
// to a made-up address. The login stays retryable.
TEST(PhoneMigrationFakeDc, InvalidMigrationDcFailsWithoutRetry) {
    PhoneDcs dcs;
    dcs.home.set_handler([&dcs](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::auth_sendCode::kId) return false;
        ++dcs.home_send;
        write_rpc_error(out, 303, "PHONE_MIGRATE_9");
        return true;
    });
    Client client(options_for(dcs.home, dcs.dc5, temp_session_path("invalid.session")));
    client.connect();
    EXPECT_THROW(client.send_login_code(kPhone), std::runtime_error);
    EXPECT_EQ(dcs.home_send.load(), 1);
    EXPECT_EQ(dcs.dc5_send.load(), 0);
    EXPECT_TRUE(cppgram::auth_can_login(client.auth_state())) << "a failed migration must leave the login retryable";
    client.disconnect();
}

// A DC that keeps answering PHONE_MIGRATE_5 must not cause an endless loop. Two moves are allowed, so the request is
// sent three times in total, and then the error is returned.
TEST(PhoneMigrationFakeDc, RepeatedMigrationExhaustsTheRetries) {
    PhoneDcs dcs;
    dcs.dc5.set_handler([&dcs](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::auth_sendCode::kId) return false;
        ++dcs.dc5_send;
        write_rpc_error(out, 303, "PHONE_MIGRATE_5");
        return true;
    });
    Client client(options_for(dcs.home, dcs.dc5, temp_session_path("loop.session")));
    client.connect();
    try {
        client.send_login_code(kPhone);
        FAIL() << "a migration loop must end with an error";
    } catch (const cppgram::mtproto::RpcError& e) {
        EXPECT_EQ(e.rpc_message(), "PHONE_MIGRATE_5");
    }
    EXPECT_EQ(dcs.home_send.load(), 1);
    EXPECT_EQ(dcs.dc5_send.load(), 2);
    EXPECT_NE(client.auth_state(), AuthState::CodeRequired);
    client.disconnect();
}

// DC 5 refuses connections. The migration fails with a connection error, the connection is down afterwards, and no
// login state is kept.
TEST(PhoneMigrationFakeDc, ConnectionFailureDuringMigrationLeavesNoLoginState) {
    PhoneDcs dcs;
    Client client(options_for(dcs.home, dcs.dc5, temp_session_path("unreachable.session")));
    client.connect();
    dcs.dc5.set_refusing(true);  // new connections to DC 5 are closed at once, so the handshake fails
    EXPECT_ANY_THROW(client.send_login_code(kPhone));
    EXPECT_EQ(dcs.home_send.load(), 1);
    EXPECT_FALSE(client.is_connected());
    EXPECT_NE(client.auth_state(), AuthState::CodeRequired);
    client.disconnect();
}

// Regression for the saved-session fix, with the phone flow: after a migrated code request, a restarted client must
// dial DC 5 (the saved DC), not the DC in its options.
TEST(PhoneMigrationFakeDc, RestartAfterPhoneMigrationDialsTheSavedDc) {
    PhoneDcs dcs;
    const std::string path = temp_session_path("restart.session");
    {
        Client first(options_for(dcs.home, dcs.dc5, path));
        first.connect();
        first.send_login_code(kPhone);
        first.disconnect();
    }
    const int home_before = dcs.home.connections_accepted();
    const int dc5_before = dcs.dc5.connections_accepted();
    {
        Client second(options_for(dcs.home, dcs.dc5, path));
        ASSERT_NO_THROW(second.connect());
        second.disconnect();
    }
    EXPECT_EQ(dcs.home.connections_accepted(), home_before) << "the saved DC 5 session must not dial DC 2";
    EXPECT_GT(dcs.dc5.connections_accepted(), dc5_before);
}

// The code request was answered by DC 5. A later bot login is moved to DC 3 by USER_MIGRATE_3. That move keeps the
// pending code, and sign_in_code goes back to DC 5 before it submits the code. DC 3 never sees the sign-in.
TEST(PhoneMigrationFakeDc, SignInAfterAnotherMigrationStillUsesTheIssuingDc) {
    PhoneDcs dcs;
    FakeDc dc3;
    std::atomic<int> dc5_import{0};
    std::atomic<int> dc3_import{0};
    std::atomic<int> dc3_sign_in{0};
    dcs.dc5.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id == fn::auth_sendCode::kId) {
            ++dcs.dc5_send;
            std::lock_guard<std::mutex> lock(dcs.mu);
            dcs.dc5_phone = args.read_string();
            write_sent_code(out, "hash-5");
            return true;
        }
        if (id == fn::auth_signIn::kId) {
            ++dcs.dc5_sign_in;
            const SignInArgs s = read_sign_in(args);
            if (s.hash != "hash-5" || s.code != kCode) {
                write_rpc_error(out, 400, "PHONE_CODE_INVALID");
            } else {
                write_auth_ok(out);
            }
            return true;
        }
        if (id == fn::auth_importBotAuthorization::kId) {
            ++dc5_import;
            write_rpc_error(out, 303, "USER_MIGRATE_3");
            return true;
        }
        return false;
    });
    dc3.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id == fn::auth_importBotAuthorization::kId) {
            ++dc3_import;
            write_rpc_error(out, 400, "ACCESS_TOKEN_INVALID");
            return true;
        }
        if (id == fn::auth_signIn::kId) {
            ++dc3_sign_in;
            write_rpc_error(out, 400, "PHONE_CODE_INVALID");
            return true;
        }
        return false;
    });

    ClientOptions o = options_for(dcs.home, dcs.dc5, temp_session_path("hashdc.session"));
    o.server_keys.push_back(dc3.public_key());
    o.dc_addresses = {DcAddress{5, "127.0.0.1", dcs.dc5.port()}, DcAddress{3, "127.0.0.1", dc3.port()}};
    Client client(o);
    client.connect();
    client.send_login_code(kPhone);
    EXPECT_EQ(client.auth_state(), AuthState::CodeRequired);

    EXPECT_THROW(client.login_bot("123456:TEST_TOKEN_NOT_REAL"), cppgram::mtproto::RpcError);
    EXPECT_EQ(dc5_import.load(), 1);
    EXPECT_EQ(dc3_import.load(), 1);

    EXPECT_EQ(client.auth_state(), AuthState::CodeRequired) << "the migration must keep the pending code";
    bool ok = false;
    ASSERT_NO_THROW(ok = client.sign_in_code(kPhone, kCode, "hash-5"));
    EXPECT_TRUE(ok);
    EXPECT_EQ(client.auth_state(), AuthState::Authorized);
    EXPECT_EQ(dcs.dc5_sign_in.load(), 1) << "sign-in must go back to the DC that issued the hash";
    EXPECT_EQ(dc3_sign_in.load(), 0) << "sign-in must not be sent to the DC the connection moved to";
    client.disconnect();
}
