#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>
#include <string>

#include "cppgram/session_string.h"

using namespace cppgram;

namespace {

void set_pass(const char* v) {
    if (v == nullptr) ::unsetenv("CPPGRAM_SESSION_PASSPHRASE");
    else ::setenv("CPPGRAM_SESSION_PASSPHRASE", v, 1);
}

struct EnvGuard {
    ~EnvGuard() { set_pass(nullptr); }
};

mtproto::SessionData sample() {
    mtproto::SessionData d;
    d.dc_id = 3;
    d.time_offset = -5;
    d.server_salt = 99;
    d.auth_key.assign(256, 0x5A);
    return d;
}

}  // namespace

TEST(SessionStringEncrypted, PlainWithoutPassphraseAsBefore) {
    EnvGuard guard;
    set_pass(nullptr);
    const std::string s = encode_session_string(sample());
    EXPECT_NE(s.rfind("cge1:", 0), 0u);
    mtproto::SessionData out;
    ASSERT_TRUE(decode_session_string(s, out));
    EXPECT_EQ(out.auth_key, sample().auth_key);
}

TEST(SessionStringEncrypted, EncryptedRoundTripWithPassphrase) {
    EnvGuard guard;
    set_pass("string-pass-123");
    const std::string s = encode_session_string(sample());
    EXPECT_EQ(s.rfind("cge1:", 0), 0u);
    set_pass(nullptr);
    const std::string plain = encode_session_string(sample());
    // The plain base64 of the session must not appear inside the encrypted text.
    EXPECT_EQ(s.find(plain.substr(4, 40)), std::string::npos);
    set_pass("string-pass-123");
    mtproto::SessionData out;
    ASSERT_TRUE(decode_session_string(s, out));
    EXPECT_EQ(out.auth_key, sample().auth_key);
    EXPECT_EQ(out.dc_id, 3);
    EXPECT_EQ(out.time_offset, -5);
    EXPECT_EQ(out.server_salt, 99u);
}

TEST(SessionStringEncrypted, EncryptedStringWithoutPassphraseIsAnError) {
    EnvGuard guard;
    set_pass("p1");
    const std::string s = encode_session_string(sample());
    set_pass(nullptr);
    mtproto::SessionData out;
    EXPECT_THROW(decode_session_string(s, out), std::runtime_error);
}

TEST(SessionStringEncrypted, WrongPassphraseIsAnError) {
    EnvGuard guard;
    set_pass("p1");
    const std::string s = encode_session_string(sample());
    set_pass("p2");
    mtproto::SessionData out;
    EXPECT_THROW(decode_session_string(s, out), std::runtime_error);
}

TEST(SessionStringEncrypted, TamperedEncryptedStringIsRejected) {
    EnvGuard guard;
    set_pass("p1");
    std::string s = encode_session_string(sample());
    // Change one character well inside the payload; base64 stays valid, the tag must fail.
    s[s.size() / 2] = s[s.size() / 2] == 'A' ? 'B' : 'A';
    mtproto::SessionData out;
    EXPECT_ANY_THROW({
        if (!decode_session_string(s, out)) throw std::runtime_error("malformed");
    });
}
