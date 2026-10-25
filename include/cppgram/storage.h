// SQLite-backed session storage: auth key, DC, server salt, and the update state (pts/qts/date/seq).
// Used when ClientOptions::session_path ends in .sqlite, .sqlite3 or .db.
#pragma once

#include <cstdint>
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
    sqlite3* db_ = nullptr;
};

}  // namespace cppgram
