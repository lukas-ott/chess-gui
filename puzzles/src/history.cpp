#include "kiebitz/puzzles/history.hpp"

#include "kiebitz/sql/sqlite.hpp"

#include <algorithm>
#include <sqlite3.h>

namespace kiebitz::puzzles {

struct History::Impl {
    sqlite3* db = nullptr;
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() { sqlite3_close(db); }
};

using sql::Statement;

History::History(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
History::~History() = default;

core::Result<std::unique_ptr<History>> History::open(const std::string& path) {
    auto impl = std::make_unique<Impl>();
    if (sqlite3_open_v2(path.c_str(), &impl->db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        return core::Error{std::string("Couldn't open the puzzle history: ") +
                           sql::errorOf(impl->db)};
    }
    sqlite3_busy_timeout(impl->db, 2000);
    if (sqlite3_exec(impl->db,
                     "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;"
                     "CREATE TABLE IF NOT EXISTS attempts (id INTEGER PRIMARY KEY, source TEXT NOT "
                     "NULL, puzzle TEXT NOT NULL, solved INTEGER NOT NULL, clean INTEGER NOT NULL, "
                     "millis INTEGER NOT "
                     "NULL, rating INTEGER NOT NULL, at INTEGER NOT NULL);"
                     "CREATE INDEX IF NOT EXISTS attempts_source ON attempts (source, id)",
                     nullptr, nullptr, nullptr) != SQLITE_OK) {
        return core::Error{std::string("Couldn't open the puzzle history: ") +
                           sqlite3_errmsg(impl->db)};
    }
    return std::unique_ptr<History>(new History(std::move(impl)));
}

void History::record(const Attempt& attempt) {
    Statement insert;
    if (!insert.prepare(m_impl->db,
                        "INSERT INTO attempts (source, puzzle, solved, clean, millis, rating, at) "
                        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)")) {
        return;
    }
    sql::bindText(insert.handle, 1, attempt.source);
    sql::bindText(insert.handle, 2, attempt.puzzle);
    sqlite3_bind_int(insert.handle, 3, attempt.solved ? 1 : 0);
    sqlite3_bind_int(insert.handle, 4, attempt.clean ? 1 : 0);
    sqlite3_bind_int64(insert.handle, 5, attempt.millis);
    sqlite3_bind_int(insert.handle, 6, attempt.rating);
    sqlite3_bind_int64(insert.handle, 7, attempt.when);
    sqlite3_step(insert.handle);
}

std::vector<Attempt> History::recent(const std::string& source, int limit) const {
    std::vector<Attempt> result;
    Statement query;
    if (!query.prepare(m_impl->db,
                       "SELECT puzzle, solved, clean, millis, rating, at FROM attempts WHERE "
                       "source = ? ORDER BY id DESC LIMIT ?")) {
        return result;
    }
    sql::bindText(query.handle, 1, source);
    sqlite3_bind_int(query.handle, 2, limit);
    while (sqlite3_step(query.handle) == SQLITE_ROW) {
        Attempt attempt;
        attempt.source = source;
        attempt.puzzle = sql::columnText(query.handle, 0);
        attempt.solved = sqlite3_column_int(query.handle, 1) != 0;
        attempt.clean = sqlite3_column_int(query.handle, 2) != 0;
        attempt.millis = sqlite3_column_int64(query.handle, 3);
        attempt.rating = sqlite3_column_int(query.handle, 4);
        attempt.when = sqlite3_column_int64(query.handle, 5);
        result.push_back(std::move(attempt));
    }
    return result;
}

Totals History::totals(const std::string& source) const {
    Totals totals;
    Statement query;
    if (!query.prepare(m_impl->db,
                       "SELECT clean, millis, solved FROM attempts WHERE source = ? ORDER BY id")) {
        return totals;
    }
    sql::bindText(query.handle, 1, source);
    std::int64_t millis = 0;
    int timed = 0;
    int run = 0;
    while (sqlite3_step(query.handle) == SQLITE_ROW) {
        ++totals.attempts;
        if (const std::int64_t taken = sqlite3_column_int64(query.handle, 1); taken >= 0) {
            millis += taken; // not the ones solved with the clock stopped
            ++timed;
        }
        totals.solved += sqlite3_column_int(query.handle, 2) != 0 ? 1 : 0;
        if (sqlite3_column_int(query.handle, 0) != 0) {
            ++totals.clean;
            ++run;
            totals.bestStreak = std::max(totals.bestStreak, run);
        } else {
            run = 0;
        }
    }
    totals.streak = run;
    totals.averageMillis = timed > 0 ? millis / timed : 0;
    return totals;
}

std::map<std::string, bool> History::latest(const std::string& source) const {
    std::map<std::string, bool> result;
    Statement query;
    if (!query.prepare(m_impl->db,
                       "SELECT puzzle, clean FROM attempts WHERE source = ? ORDER BY id")) {
        return result;
    }
    sql::bindText(query.handle, 1, source);
    while (sqlite3_step(query.handle) == SQLITE_ROW) {
        result[sql::columnText(query.handle, 0)] = sqlite3_column_int(query.handle, 1) != 0;
    }
    return result;
}

void History::clear(const std::string& source) {
    Statement remove;
    if (remove.prepare(m_impl->db, "DELETE FROM attempts WHERE source = ?")) {
        sql::bindText(remove.handle, 1, source);
        sqlite3_step(remove.handle);
    }
}

} // namespace kiebitz::puzzles
