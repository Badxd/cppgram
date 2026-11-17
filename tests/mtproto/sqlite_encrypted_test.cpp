// SQLite session store with an encrypted auth key (CGE1 envelope in the auth_key column).
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "cppgram/mtproto/session.h"
#include "cppgram/storage.h"

using namespace cppgram;

namespace {

std::string fresh_db(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / "cppgram_sqlite_enc_test";
    std::filesystem::create_directories(dir);
    const auto path = (dir / name).string();
    for (const char* suffix : {"", "-wal", "-shm"}) std::remove((path + suffix).c_str());
    return path;
}

void set_passphrase(const char* value) {
    if (value == nullptr) {
        ::unsetenv("CPPGRAM_SESSION_PASSPHRASE");
    } else {
        ::setenv("CPPGRAM_SESSION_PASSPHRASE", value, 1);
    }
}

struct EnvGuard {
    ~EnvGuard() { set_passphrase(nullptr); }
};

mtproto::SessionData sample() {
    mtproto::SessionData d;
    d.dc_id = 4;
    d.time_offset = 7;
    d.server_salt = 42;
    d.auth_key.assign(256, 0xCD);
    return d;
}

bool file_contains_run(const std::string& path, std::uint8_t value, std::size_t run_len) {
    std::ifstream in(path, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t run = 0;
    for (char c : bytes) {
        run = static_cast<std::uint8_t>(c) == value ? run + 1 : 0;
        if (run >= run_len) return true;
    }
    return false;
}

}  // namespace

TEST(SqliteEncrypted, KeyIsStoredEncryptedAndRoundTrips) {
    EnvGuard guard;
    const auto path = fresh_db("enc.sqlite");
    set_passphrase("sqlite-pass-value");
    {
        SqliteStorage store(path);
        store.save_session(sample());
    }
    EXPECT_FALSE(file_contains_run(path, 0xCD, 16)) << "auth key visible in the database file";
    SqliteStorage store(path);
    mtproto::SessionData loaded;
    ASSERT_TRUE(store.load_session(loaded));
    EXPECT_EQ(loaded.auth_key, sample().auth_key);
    EXPECT_EQ(loaded.dc_id, 4);
    EXPECT_EQ(loaded.server_salt, 42u);
}

TEST(SqliteEncrypted, EncryptedKeyNeedsPassphrase) {
    EnvGuard guard;
    const auto path = fresh_db("needs.sqlite");
    set_passphrase("pw1");
    { SqliteStorage store(path); store.save_session(sample()); }
    set_passphrase(nullptr);
    SqliteStorage store(path);
    mtproto::SessionData out;
    EXPECT_THROW(store.load_session(out), mtproto::SessionCorruptError);
}

TEST(SqliteEncrypted, WrongPassphraseIsCorrupt) {
    EnvGuard guard;
    const auto path = fresh_db("wrong.sqlite");
    set_passphrase("right-pass");
    { SqliteStorage store(path); store.save_session(sample()); }
    set_passphrase("wrong-pass");
    SqliteStorage store(path);
    mtproto::SessionData out;
    EXPECT_THROW(store.load_session(out), mtproto::SessionCorruptError);
}

TEST(SqliteEncrypted, PlainKeyStillLoadsAndIsEncryptedOnNextSave) {
    EnvGuard guard;
    const auto path = fresh_db("migrate.sqlite");
    set_passphrase(nullptr);
    { SqliteStorage store(path); store.save_session(sample()); }
    EXPECT_TRUE(file_contains_run(path, 0xCD, 16)) << "plain store should hold the raw key";

    set_passphrase("migrate-pass");
    {
        SqliteStorage store(path);
        mtproto::SessionData loaded;
        ASSERT_TRUE(store.load_session(loaded));
        EXPECT_EQ(loaded.auth_key, sample().auth_key);
        store.save_session(loaded);
    }
    EXPECT_FALSE(file_contains_run(path, 0xCD, 16));
    SqliteStorage store(path);
    mtproto::SessionData again;
    ASSERT_TRUE(store.load_session(again));
    EXPECT_EQ(again.auth_key, sample().auth_key);
}
