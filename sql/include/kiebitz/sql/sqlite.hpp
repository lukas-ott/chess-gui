#pragma once

#include <sqlite3.h>
#include <string>
#include <string_view>
#include <utility>

namespace kiebitz::sql {

/// A prepared statement, finalized when it goes. `handle` is null if preparing failed (SQLite's
/// calls accept null and do nothing).
struct Statement {
    sqlite3_stmt* handle = nullptr;

    Statement() = default;
    Statement(sqlite3* db, const char* sql) { prepare(db, sql); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    Statement& operator=(Statement&& other) noexcept {
        std::swap(handle, other.handle);
        return *this;
    }
    ~Statement() { sqlite3_finalize(handle); }

    /// Prepares `sql` (finalizing what was there). False on an error; the handle is then null.
    bool prepare(sqlite3* db, const char* sql) {
        sqlite3_finalize(std::exchange(handle, nullptr));
        if (sqlite3_prepare_v2(db, sql, -1, &handle, nullptr) != SQLITE_OK) {
            sqlite3_finalize(std::exchange(handle, nullptr));
            return false;
        }
        return true;
    }
};

/// Runs SQL that returns no rows (one or more statements). False on an error.
inline bool exec(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

/// Binds text (copied, so `text` need not outlive the statement). Empty text is bound as ''.
inline int bindText(sqlite3_stmt* statement, int index, std::string_view text) {
    return sqlite3_bind_text(statement, index, text.data(), static_cast<int>(text.size()),
                             SQLITE_TRANSIENT);
}

/// Binds text, or NULL when it is empty.
inline int bindTextOrNull(sqlite3_stmt* statement, int index, std::string_view text) {
    return text.empty() ? sqlite3_bind_null(statement, index) : bindText(statement, index, text);
}

/// A text (or blob) column as a string; NULL gives "". (Read as a blob: the same bytes, handed
/// out as char where text comes as unsigned char.)
inline std::string columnText(sqlite3_stmt* statement, int column) {
    const auto* bytes = static_cast<const char*>(sqlite3_column_blob(statement, column));
    return bytes == nullptr
               ? std::string()
               : std::string(bytes,
                             static_cast<std::size_t>(sqlite3_column_bytes(statement, column)));
}

/// SQLite's last error on `db`, for messages (a database that couldn't be allocated has none).
inline std::string errorOf(sqlite3* db) {
    return db != nullptr ? sqlite3_errmsg(db) : "out of memory";
}

} // namespace kiebitz::sql
