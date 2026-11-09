// Client::recover_password against a scripted fake DC. Offline only. NOT RUN in this session: the owner runs the suite.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>

#include "cppgram/client.h"
#include "cppgram/mtproto/schema/api.h"
#include "fake_dc.h"

using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;

namespace {

constexpr std::uint32_t kAccountPasswordId = 0x957b50fbu;   // account.password
constexpr std::uint32_t kKdfAlgoId = 0x3a912d4au;           // passwordKdfAlgoSHA256...ModPow
constexpr std::uint32_t kSecureKdfUnknownId = 0x004a8537u;  // securePasswordKdfAlgoUnknown
constexpr std::uint32_t kAuthorizationId = 0x2ea2c0d4u;     // auth.authorization
constexpr std::uint32_t kUserId = 0xb1b8cc83u;              // user
constexpr std::uint32_t kPasswordInputSettingsId = 0xc23727c9u;  // account.passwordInputSettings

std::string session_path(const char* tag) {
    static std::atomic<int> counter{0};
    return (std::filesystem::temp_directory_path() /
            ("cppgram-recover-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
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

// auth.authorization with a user that has only the required id.
void write_authorization(cppgram::mtproto::TlWriter& out, std::int64_t user_id) {
    out.write_uint32(kAuthorizationId);
    out.write_uint32(0);  // flags
    out.write_uint32(kUserId);
    out.write_uint32(0);  // user flags
    out.write_uint32(0);  // user flags2
    out.write_int64(user_id);
}

// account.password with no current algo and a new algo the client can derive a verifier from.
void write_password_without_current(cppgram::mtproto::TlWriter& out) {
    out.write_uint32(kAccountPasswordId);
    out.write_uint32(0);  // flags: no current password
    out.write_uint32(kKdfAlgoId);
    out.write_bytes({ 0x01, 0x02, 0x03, 0x04 });  // salt1
    out.write_bytes({ 0x05, 0x06 });              // salt2
    out.write_int32(3);                           // g
    cppgram::mtproto::Bytes p(256, 0xff);
    p.back() = 0xfd;
    out.write_bytes(p);
    out.write_uint32(kSecureKdfUnknownId);
    out.write_bytes({ 0x00 });  // secure_random
}

using Handler = std::function<bool(std::uint32_t, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter&)>;

// Answers the login steps that lead to the 2FA prompt (auth.sendCode, then auth.signIn with
// SESSION_PASSWORD_NEEDED) and passes every other request to `next`. Recovery only follows that prompt.
Handler with_2fa_prompt(Handler next) {
    return [next = std::move(next)](std::uint32_t id, cppgram::mtproto::TlReader& args,
                                    cppgram::mtproto::TlWriter& out) {
        if (id == fn::auth_sendCode::kId) {
            cppgram::mtproto::tl::auth_SentCode res;
            cppgram::mtproto::tl::types::auth_sentCode sent;
            sent.phone_code_hash = "hash-1";
            sent.timeout = 60;
            sent.type = std::make_shared<cppgram::mtproto::tl::auth_SentCodeType>();
            sent.type->value = cppgram::mtproto::tl::types::auth_sentCodeTypeSms{5};
            res.value = sent;
            res.serialize(out);
            return true;
        }
        if (id == fn::auth_signIn::kId) {
            cppgram::mtproto::tl::types::rpc_error err;
            err.error_code = 401;
            err.error_message = "SESSION_PASSWORD_NEEDED";
            err.serialize(out);
            return true;
        }
        return next(id, args, out);
    };
}

// Drives the public login API to the 2FA prompt. sign_in_code returns false exactly when the prompt is reached.
void enter_2fa_prompt(Client& client) {
    client.send_login_code("+10000000000");
    if (client.sign_in_code("+10000000000", "12345", "hash-1")) {
        throw std::logic_error("expected the 2FA prompt, got a signed-in session");
    }
}

} // namespace

TEST(PasswordRecoveryFakeDc, RecoverWithoutNewPasswordSendsNoSettings) {
    FakeDc dc;
    std::uint32_t seen_flags = 1234;
    std::string seen_code;
    dc.set_handler(with_2fa_prompt([&](std::uint32_t id, cppgram::mtproto::TlReader& args,
                                       cppgram::mtproto::TlWriter& out) {
        if (id != fn::auth_recoverPassword::kId) return false;
        seen_flags = args.read_uint32();
        seen_code = args.read_string();
        write_authorization(out, 77);
        return true;
    }));

    const std::string path = session_path("plain");
    Client client(options_for(dc, path));
    client.connect();
    enter_2fa_prompt(client);
    const std::int64_t id = client.recover_password("654321");
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_EQ(id, 77);
    EXPECT_EQ(seen_flags, 0u);
    EXPECT_EQ(seen_code, "654321");
}

TEST(PasswordRecoveryFakeDc, RecoverWithNewPasswordSendsVerifierAndHint) {
    FakeDc dc;
    bool get_password_called = false;
    std::uint32_t seen_flags = 0;
    std::string seen_code;
    std::uint32_t seen_settings_ctor = 0;
    std::uint32_t seen_settings_flags = 0;
    std::uint32_t seen_settings_algo = 0;
    std::size_t seen_verifier_size = 0;
    std::string seen_hint;
    dc.set_handler(with_2fa_prompt([&](std::uint32_t id, cppgram::mtproto::TlReader& args,
                                       cppgram::mtproto::TlWriter& out) {
        if (id == fn::account_getPassword::kId) {
            get_password_called = true;
            write_password_without_current(out);
            return true;
        }
        if (id != fn::auth_recoverPassword::kId) return false;
        seen_flags = args.read_uint32();
        seen_code = args.read_string();
        seen_settings_ctor = args.read_uint32();
        seen_settings_flags = args.read_uint32();
        seen_settings_algo = args.read_uint32();
        args.read_bytes();  // new_algo.salt1 is checked by the verifier, not here
        args.read_bytes();  // salt2
        args.read_int32();  // g
        args.read_bytes();  // p
        seen_verifier_size = args.read_bytes().size();
        seen_hint = args.read_string();
        write_authorization(out, 88);
        return true;
    }));

    const std::string path = session_path("newpw");
    Client client(options_for(dc, path));
    client.connect();
    enter_2fa_prompt(client);
    const std::int64_t id = client.recover_password("654321", "new secret", "pet");
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_TRUE(get_password_called);
    EXPECT_EQ(id, 88);
    EXPECT_EQ(seen_flags, 1u) << "auth.recoverPassword must set new_settings (flags.0)";
    EXPECT_EQ(seen_code, "654321");
    EXPECT_EQ(seen_settings_ctor, kPasswordInputSettingsId);
    EXPECT_EQ(seen_settings_flags, 1u) << "new_algo, new_password_hash and hint are all set";
    EXPECT_EQ(seen_settings_algo, kKdfAlgoId);
    EXPECT_EQ(seen_verifier_size, 256u);
    EXPECT_EQ(seen_hint, "pet");
}

TEST(PasswordRecoveryFakeDc, RecoverRejectsEmptyCodeBeforeAnyRpc) {
    FakeDc dc;
    bool rpc_seen = false;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter&) {
        if (id == fn::auth_recoverPassword::kId) rpc_seen = true;
        return false;
    });

    const std::string path = session_path("empty");
    Client client(options_for(dc, path));
    client.connect();
    EXPECT_THROW(client.recover_password(""), std::invalid_argument);
    client.disconnect();
    std::filesystem::remove(path);

    EXPECT_FALSE(rpc_seen);
}
