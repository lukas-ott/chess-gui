#include "kiebitz/library/training_stats.hpp"

#include <sqlite3.h>

namespace kiebitz::library {

struct TrainingStats::Impl {
    sqlite3* db = nullptr;
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() { sqlite3_close(db); }
};

namespace {

struct Statement {
    sqlite3_stmt* handle = nullptr;
    Statement() = default;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&&) = delete;
    Statement& operator=(Statement&&) = delete;
    ~Statement() { sqlite3_finalize(handle); }
};

// SQLite stores signed 64-bit integers; a position hash is unsigned. The conversion keeps the
// bits (two's complement, as C++20 defines it), so it comes back the same.
sqlite3_int64 key(std::uint64_t position) {
    return static_cast<sqlite3_int64>(position);
}
int sideOf(core::Color side) {
    return side == core::Color::White ? 0 : 1;
}

} // namespace

TrainingStats::TrainingStats(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
TrainingStats::~TrainingStats() = default;

core::Result<std::unique_ptr<TrainingStats>> TrainingStats::open(const std::string& path) {
    auto impl = std::make_unique<Impl>();
    if (sqlite3_open_v2(path.c_str(), &impl->db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        return core::Error{std::string("Couldn't open the practice statistics: ") +
                           (impl->db != nullptr ? sqlite3_errmsg(impl->db) : "out of memory")};
    }
    sqlite3_busy_timeout(impl->db, 2000);
    if (sqlite3_exec(impl->db,
                     "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;"
                     "CREATE TABLE IF NOT EXISTS positions (side INTEGER NOT NULL, position "
                     "INTEGER NOT NULL, tries INTEGER NOT NULL, mistakes INTEGER NOT NULL, "
                     "streak INTEGER NOT NULL, PRIMARY KEY (side, position))",
                     nullptr, nullptr, nullptr) != SQLITE_OK) {
        return core::Error{std::string("Couldn't open the practice statistics: ") +
                           sqlite3_errmsg(impl->db)};
    }
    return std::unique_ptr<TrainingStats>(new TrainingStats(std::move(impl)));
}

void TrainingStats::record(core::Color side, std::uint64_t position, bool right) {
    Statement upsert;
    if (sqlite3_prepare_v2(m_impl->db,
                           "INSERT INTO positions VALUES (?1, ?2, 1, ?3, ?4) "
                           "ON CONFLICT (side, position) DO UPDATE SET tries = tries + 1, "
                           "mistakes = mistakes + ?3, streak = CASE WHEN ?3 = 1 THEN 0 "
                           "ELSE streak + 1 END",
                           -1, &upsert.handle, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_int(upsert.handle, 1, sideOf(side));
    sqlite3_bind_int64(upsert.handle, 2, key(position));
    sqlite3_bind_int(upsert.handle, 3, right ? 0 : 1);
    sqlite3_bind_int(upsert.handle, 4, right ? 1 : 0);
    sqlite3_step(upsert.handle);
}

PositionStats TrainingStats::get(core::Color side, std::uint64_t position) const {
    PositionStats stats;
    Statement query;
    if (sqlite3_prepare_v2(m_impl->db,
                           "SELECT tries, mistakes, streak FROM positions WHERE side = ? AND "
                           "position = ?",
                           -1, &query.handle, nullptr) != SQLITE_OK) {
        return stats;
    }
    sqlite3_bind_int(query.handle, 1, sideOf(side));
    sqlite3_bind_int64(query.handle, 2, key(position));
    if (sqlite3_step(query.handle) == SQLITE_ROW) {
        stats.tries = sqlite3_column_int(query.handle, 0);
        stats.mistakes = sqlite3_column_int(query.handle, 1);
        stats.streak = sqlite3_column_int(query.handle, 2);
    }
    return stats;
}

std::unordered_set<std::uint64_t> TrainingStats::mistakes(core::Color side) const {
    std::unordered_set<std::uint64_t> result;
    Statement query;
    if (sqlite3_prepare_v2(m_impl->db,
                           "SELECT position FROM positions WHERE side = ? AND mistakes > 0 AND "
                           "streak < ?",
                           -1, &query.handle, nullptr) != SQLITE_OK) {
        return result;
    }
    sqlite3_bind_int(query.handle, 1, sideOf(side));
    sqlite3_bind_int(query.handle, 2, kSettled);
    while (sqlite3_step(query.handle) == SQLITE_ROW) {
        result.insert(static_cast<std::uint64_t>(sqlite3_column_int64(query.handle, 0)));
    }
    return result;
}

} // namespace kiebitz::library
