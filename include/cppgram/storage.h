// SQLite-backed session storage: auth key, DC, server salt, and the update state (pts/qts/date/seq).
// Used when ClientOptions::session_path ends in .sqlite, .sqlite3 or .db.
// All member functions are thread-safe with respect to each other on the same object. Separate objects may use the same
// file: WAL mode and a 5 s busy timeout handle the competing writers.
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "cppgram/mtproto/session.h"

struct sqlite3;

namespace cppgram {

struct StoredUpdateState {
    std::int32_t pts = 0;
    std::int32_t qts = 0;
    std::int32_t date = 0;
    std::int32_t seq = 0;
};

class SqliteStorage {
public:
    explicit SqliteStorage(const std::string& path);
    ~SqliteStorage();
    SqliteStorage(const SqliteStorage&) = delete;
    SqliteStorage& operator=(const SqliteStorage&) = delete;

    // Returns false when no auth key is stored yet.
    bool load_session(mtproto::SessionData& out) const;
    void save_session(const mtproto::SessionData& data);

    std::optional<StoredUpdateState> load_state() const;
    void save_state(const StoredUpdateState& state);

private:
    // Serializes every call on the connection. SQLite's own threading mode does not protect this wrapper, and
    // load_* are const, so the mutex is mutable.
    mutable std::mutex mutex_;
    sqlite3* db_ = nullptr;

    // Opens, configures and checks the store. Called from the constructor, which closes db_ on any throw.
    void open_store(const std::string& path);
};

}  // namespace cppgram
