#include "kiebitz/training/log.hpp"

#include "kiebitz/sql/sqlite.hpp"

#include <algorithm>
#include <cstddef>
#include <sqlite3.h>

namespace kiebitz::training {

struct Log::Impl {
    sqlite3* db = nullptr;
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() { sqlite3_close(db); }
};

namespace {

/// A part's index in kAllParts (and in DaySeconds).
std::size_t indexOf(Part part) {
    return static_cast<std::size_t>(std::find(kAllParts.begin(), kAllParts.end(), part) -
                                    kAllParts.begin());
}

} // namespace

int streak(const std::set<Day>& days, Day today) {
    Day day = days.contains(today) ? today : today - 1;
    int count = 0;
    while (days.contains(day)) {
        ++count;
        --day;
    }
    return count;
}

int bestStreak(const std::set<Day>& days) {
    int best = 0;
    int run = 0;
    Day previous = 0;
    for (const Day day : days) {
        run = run > 0 && day == previous + 1 ? run + 1 : 1;
        best = std::max(best, run);
        previous = day;
    }
    return best;
}

Log::Log(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
Log::~Log() = default;

core::Result<std::unique_ptr<Log>> Log::open(const std::string& path) {
    auto impl = std::make_unique<Impl>();
    if (sqlite3_open_v2(path.c_str(), &impl->db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        return core::Error{"Couldn't open the training log: " + sql::errorOf(impl->db)};
    }
    sqlite3_busy_timeout(impl->db, 2000);
    if (!sql::exec(impl->db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;"
                             "CREATE TABLE IF NOT EXISTS trained (day INTEGER NOT NULL, part TEXT "
                             "NOT NULL, seconds INTEGER NOT NULL, PRIMARY KEY (day, part))")) {
        return core::Error{"Couldn't open the training log: " + sql::errorOf(impl->db)};
    }
    return std::unique_ptr<Log>(new Log(std::move(impl)));
}

void Log::add(Day day, Part part, int seconds) {
    if (seconds <= 0) {
        return;
    }
    const sql::Statement upsert(m_impl->db,
                                "INSERT INTO trained VALUES (?, ?, ?) ON CONFLICT (day, part) "
                                "DO UPDATE SET seconds = seconds + excluded.seconds");
    sqlite3_bind_int(upsert.handle, 1, day);
    sql::bindText(upsert.handle, 2, key(part));
    sqlite3_bind_int(upsert.handle, 3, seconds);
    sqlite3_step(upsert.handle);
}

std::map<Day, DaySeconds> Log::days(Day from) const {
    std::map<Day, DaySeconds> result;
    const sql::Statement query(
        m_impl->db, "SELECT day, part, seconds FROM trained WHERE day >= ? ORDER BY day");
    sqlite3_bind_int(query.handle, 1, from);
    while (query.handle != nullptr && sqlite3_step(query.handle) == SQLITE_ROW) {
        const Day day = sqlite3_column_int(query.handle, 0);
        const std::string name = sql::columnText(query.handle, 1);
        for (const Part part : kAllParts) {
            if (key(part) == name) {
                result[day][indexOf(part)] += sqlite3_column_int(query.handle, 2);
            }
        }
    }
    return result;
}

std::set<Day> Log::trainedDays() const {
    std::set<Day> result;
    const sql::Statement query(m_impl->db,
                               "SELECT day FROM trained GROUP BY day HAVING SUM(seconds) > 0");
    while (query.handle != nullptr && sqlite3_step(query.handle) == SQLITE_ROW) {
        result.insert(sqlite3_column_int(query.handle, 0));
    }
    return result;
}

} // namespace kiebitz::training
