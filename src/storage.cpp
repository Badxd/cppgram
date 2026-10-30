#include "cppgram/storage.h"

#include <sqlite3.h>

#include "secure_file.h"

#include <stdexcept>

namespace cppgram {

namespace {

void check(int rc, sqlite3* db, const char* what) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw std::runtime_error(std::string(what) + ": " + sqlite3_errmsg(db));
    }
}

}  // namespace

SqliteStorage::SqliteStorage(const std::string& path) {
    detail::ensure_private_file(path);
    check(sqlite3_open(path.c_str(), &db_), db_, "open sqlite session store");
    const char* schema =
        "CREATE TABLE IF NOT EXISTS session ("
        " id INTEGER PRIMARY KEY CHECK (id = 1),"
        " dc_id INTEGER NOT NULL,"
        " auth_key BLOB NOT NULL,"
        " server_salt INTEGER NOT NULL,"
        " time_offset INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS update_state ("
        " id INTEGER PRIMARY KEY CHECK (id = 1),"
        " pts INTEGER NOT NULL, qts INTEGER NOT NULL, date INTEGER NOT NULL, seq INTEGER NOT NULL);";
    char* err = nullptr;
    if (sqlite3_exec(db_, schema, nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("create sqlite schema: " + msg);
    }
}

SqliteStorage::~SqliteStorage() {
    if (db_) sqlite3_close(db_);
}

bool SqliteStorage::load_session(mtproto::SessionData& out) const {
    sqlite3_stmt* stmt = nullptr;
    check(sqlite3_prepare_v2(db_, "SELECT dc_id, auth_key, server_salt, time_offset FROM session WHERE id = 1", -1, &stmt, nullptr),
          db_, "prepare load_session");
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.dc_id = sqlite3_column_int(stmt, 0);
        const auto* blob = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt, 1));
        const int size = sqlite3_column_bytes(stmt, 1);
        out.auth_key.assign(blob, blob + size);
        out.server_salt = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 2));
        out.time_offset = sqlite3_column_int64(stmt, 3);
        found = out.auth_key.size() == 256;
    }
    sqlite3_finalize(stmt);
    return found;
}

void SqliteStorage::save_session(const mtproto::SessionData& data) {
    sqlite3_stmt* stmt = nullptr;
    check(sqlite3_prepare_v2(db_,
                             "INSERT OR REPLACE INTO session (id, dc_id, auth_key, server_salt, time_offset) "
                             "VALUES (1, ?, ?, ?, ?)",
                             -1, &stmt, nullptr),
          db_, "prepare save_session");
    sqlite3_bind_int(stmt, 1, data.dc_id);
    sqlite3_bind_blob(stmt, 2, data.auth_key.data(), static_cast<int>(data.auth_key.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(data.server_salt));
    sqlite3_bind_int64(stmt, 4, data.time_offset);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    check(rc, db_, "save_session");
}

std::optional<StoredUpdateState> SqliteStorage::load_state() const {
    sqlite3_stmt* stmt = nullptr;
    check(sqlite3_prepare_v2(db_, "SELECT pts, qts, date, seq FROM update_state WHERE id = 1", -1, &stmt, nullptr),
          db_, "prepare load_state");
    std::optional<StoredUpdateState> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        StoredUpdateState st;
        st.pts = sqlite3_column_int(stmt, 0);
        st.qts = sqlite3_column_int(stmt, 1);
        st.date = sqlite3_column_int(stmt, 2);
        st.seq = sqlite3_column_int(stmt, 3);
        out = st;
    }
    sqlite3_finalize(stmt);
    return out;
}

void SqliteStorage::save_state(const StoredUpdateState& state) {
    sqlite3_stmt* stmt = nullptr;
    check(sqlite3_prepare_v2(db_,
                             "INSERT OR REPLACE INTO update_state (id, pts, qts, date, seq) VALUES (1, ?, ?, ?, ?)",
                             -1, &stmt, nullptr),
          db_, "prepare save_state");
    sqlite3_bind_int(stmt, 1, state.pts);
    sqlite3_bind_int(stmt, 2, state.qts);
    sqlite3_bind_int(stmt, 3, state.date);
    sqlite3_bind_int(stmt, 4, state.seq);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    check(rc, db_, "save_state");
}

}  // namespace cppgram
