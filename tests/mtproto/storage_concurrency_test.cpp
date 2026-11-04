// Session storage under concurrent use, interrupted files and corrupted databases. All offline.
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "cppgram/mtproto/crypto.h"
#include "cppgram/storage.h"

using cppgram::SqliteStorage;
using cppgram::StoredUpdateState;
namespace mtp = cppgram::mtproto;

namespace {

std::string db_path(const char* name) {
    const auto path = std::filesystem::temp_directory_path() / (std::string("cppgram_conc_") + name + ".sqlite");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path.string() + "-wal", ec);
    std::filesystem::remove(path.string() + "-shm", ec);
    return path.string();
}

void remove_db(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

mtp::SessionData session_with(std::uint64_t salt) {
    mtp::SessionData data;
    data.dc_id = 2;
    data.auth_key = mtp::random_bytes(256);
    data.server_salt = salt;
    data.time_offset = 0;
    return data;
}

}  // namespace

// Writers store a state whose four fields are equal. A torn read would show two different values at once.
TEST(StorageConcurrency, ConcurrentSaveAndLoadStateNeverTears) {
    const std::string path = db_path("state");
    {
        SqliteStorage storage(path);
        std::atomic<bool> failed{false};
        std::vector<std::thread> writers;
        for (int t = 0; t < 3; ++t) {
            writers.emplace_back([&storage, t] {
                for (int i = 1; i <= 200; ++i) {
                    const std::int32_t v = t * 1000 + i;
                    storage.save_state(StoredUpdateState{v, v, v, v});
                }
            });
        }
        std::thread reader([&storage, &failed] {
            for (int i = 0; i < 400; ++i) {
                const auto s = storage.load_state();
                if (s && !(s->pts == s->qts && s->qts == s->date && s->date == s->seq)) failed = true;
            }
        });
        for (auto& w : writers) w.join();
        reader.join();
        EXPECT_FALSE(failed.load()) << "a reader saw fields from two different saves";
        EXPECT_TRUE(storage.load_state().has_value());
    }
    remove_db(path);
}

// Two objects on one file (WAL mode, busy timeout) must not fail or corrupt each other.
TEST(StorageConcurrency, TwoStoresOnOneFileInterleaveWithoutErrors) {
    const std::string path = db_path("two-stores");
    {
        SqliteStorage first(path);
        SqliteStorage second(path);
        std::vector<std::thread> threads;
        threads.emplace_back([&first] {
            for (int i = 0; i < 100; ++i) first.save_state(StoredUpdateState{i, i, i, i});
        });
        threads.emplace_back([&second] {
            for (int i = 0; i < 100; ++i) second.save_session(session_with(static_cast<std::uint64_t>(i + 1)));
        });
        for (auto& th : threads) th.join();
        const auto state = first.load_state();
        ASSERT_TRUE(state.has_value());
        mtp::SessionData loaded;
        ASSERT_TRUE(first.load_session(loaded));
        EXPECT_EQ(loaded.dc_id, 2);
        EXPECT_EQ(loaded.auth_key.size(), 256u);
    }
    remove_db(path);
}

// Destroying the store after all users have joined closes the handle exactly once.
TEST(StorageConcurrency, RepeatedOpenCloseCyclesLeaveAUsableFile) {
    const std::string path = db_path("cycles");
    for (int round = 0; round < 10; ++round) {
        SqliteStorage storage(path);
        storage.save_session(session_with(static_cast<std::uint64_t>(round + 1)));
    }
    SqliteStorage storage(path);
    mtp::SessionData loaded;
    ASSERT_TRUE(storage.load_session(loaded));
    EXPECT_EQ(loaded.server_salt, 10u);
    remove_db(path);
}

// A file that is not a database must be rejected at open time with a clear error, and must not be modified.
TEST(StorageConcurrency, CorruptedFileIsRejectedAndNotModified) {
    const std::string path = db_path("garbage");
    {
        std::ofstream out(path, std::ios::binary);
        out << std::string(4096, 'x');
    }
    EXPECT_THROW({ SqliteStorage storage(path); }, std::exception);
    std::ifstream in(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, std::string(4096, 'x'));
    remove_db(path);
}

// A database cut short after being written (an interrupted save) must not load as valid data.
TEST(StorageConcurrency, TruncatedDatabaseDoesNotLoadAsValidSession) {
    const std::string path = db_path("truncated");
    {
        SqliteStorage storage(path);
        storage.save_session(session_with(7));
    }
    const auto full = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, full / 2);
    std::error_code ec;
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
    bool loaded_ok = false;
    try {
        SqliteStorage storage(path);
        mtp::SessionData loaded;
        loaded_ok = storage.load_session(loaded) && loaded.server_salt == 7u;
    } catch (const std::exception&) {
        loaded_ok = false;
    }
    EXPECT_FALSE(loaded_ok);
    remove_db(path);
}

// Error messages must not echo the path contents or any key material.
TEST(StorageConcurrency, OpenErrorsDoNotLeakKeyMaterial) {
    const std::string path = db_path("no-leak");
    {
        std::ofstream out(path, std::ios::binary);
        out << "SECRET-AUTH-KEY-MATERIAL-0123456789";
    }
    try {
        SqliteStorage storage(path);
        FAIL() << "expected open to fail";
    } catch (const std::exception& e) {
        EXPECT_EQ(std::string(e.what()).find("SECRET-AUTH-KEY"), std::string::npos);
    }
    remove_db(path);
}
