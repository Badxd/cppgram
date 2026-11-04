// Encrypted session files: passphrase from CPPGRAM_SESSION_PASSPHRASE, plain-file migration, tamper detection.
#include <gtest/gtest.h>

#include <sys/stat.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "cppgram/mtproto/session.h"

using namespace cppgram::mtproto;

namespace {

std::string temp_file(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / "cppgram_enc_test";
    std::filesystem::create_directories(dir);
    const auto path = (dir / name).string();
    std::remove(path.c_str());
    return path;
}

void set_passphrase(const char* value) {
    if (value == nullptr) {
        ::unsetenv("CPPGRAM_SESSION_PASSPHRASE");
    } else {
        ::setenv("CPPGRAM_SESSION_PASSPHRASE", value, 1);
    }
}

SessionData sample() {
    SessionData d;
    d.dc_id = 2;
    d.time_offset = -12;
    d.server_salt = 0x1122334455667788ull;
    d.auth_key.assign(256, 0xAB);
    return d;
}

std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Guarantees the environment is clean even when an assertion fails.
struct EnvGuard {
    ~EnvGuard() { set_passphrase(nullptr); }
};

}  // namespace

TEST(EncryptedSession, RoundTripWithPassphrase) {
    EnvGuard guard;
    set_passphrase("long test passphrase");
    const auto path = temp_file("roundtrip.session");
    save_session_file(path, sample());
    SessionData loaded;
    ASSERT_TRUE(load_session_file(path, loaded));
    EXPECT_EQ(loaded.dc_id, 2);
    EXPECT_EQ(loaded.time_offset, -12);
    EXPECT_EQ(loaded.server_salt, 0x1122334455667788ull);
    EXPECT_EQ(loaded.auth_key, sample().auth_key);
}

TEST(EncryptedSession, FileOnDiskHoldsNoPlaintextKeyMaterial) {
    EnvGuard guard;
    set_passphrase("pw-for-disk-check");
    const auto path = temp_file("nosecret.session");
    save_session_file(path, sample());
    const auto bytes = read_bytes(path);
    ASSERT_GE(bytes.size(), 4u);
    EXPECT_EQ(std::string(bytes.begin(), bytes.begin() + 4), "CGE1");
    // A 256-byte key of 0xAB would appear as a long run of 0xAB; it must not be visible on disk.
    std::size_t run = 0, longest = 0;
    for (auto b : bytes) {
        run = b == 0xAB ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    EXPECT_LT(longest, 8u);
}

TEST(EncryptedSession, EncryptedFileNeedsPassphrase) {
    EnvGuard guard;
    const auto path = temp_file("needs-pass.session");
    set_passphrase("pw");
    save_session_file(path, sample());
    set_passphrase(nullptr);
    SessionData out;
    EXPECT_THROW(load_session_file(path, out), SessionCorruptError);
}

TEST(EncryptedSession, WrongPassphraseIsCorruptNotSilent) {
    EnvGuard guard;
    const auto path = temp_file("wrong-pass.session");
    set_passphrase("right");
    save_session_file(path, sample());
    set_passphrase("wrong");
    SessionData out;
    EXPECT_THROW(load_session_file(path, out), SessionCorruptError);
}

TEST(EncryptedSession, TamperedFileIsRejected) {
    EnvGuard guard;
    const auto path = temp_file("tamper.session");
    set_passphrase("pw");
    save_session_file(path, sample());
    auto bytes = read_bytes(path);
    bytes[bytes.size() / 2] ^= 0x40;
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    SessionData out;
    EXPECT_THROW(load_session_file(path, out), SessionCorruptError);
}

TEST(EncryptedSession, PlainFileIsReadAndMigratedOnNextSave) {
    EnvGuard guard;
    const auto path = temp_file("migrate.session");
    set_passphrase(nullptr);
    save_session_file(path, sample());  // plain CGS2
    const auto plain_bytes = read_bytes(path);
    ASSERT_GE(plain_bytes.size(), 4u);
    EXPECT_EQ(std::string(plain_bytes.begin(), plain_bytes.begin() + 4), "CGS2");

    set_passphrase("migrate-pass");
    SessionData loaded;
    ASSERT_TRUE(load_session_file(path, loaded));  // plain files still open with a passphrase set
    EXPECT_EQ(loaded.auth_key, sample().auth_key);

    save_session_file(path, loaded);  // the next save encrypts
    const auto bytes = read_bytes(path);
    EXPECT_EQ(std::string(bytes.begin(), bytes.begin() + 4), "CGE1");
    SessionData again;
    ASSERT_TRUE(load_session_file(path, again));
    EXPECT_EQ(again.auth_key, sample().auth_key);
}

TEST(EncryptedSession, EncryptedFileIsOwnerReadableOnly) {
    EnvGuard guard;
    set_passphrase("perm-pass");
    const auto path = temp_file("perm.session");
    save_session_file(path, sample());
    struct stat st{};
    ASSERT_EQ(::stat(path.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600u);
}
