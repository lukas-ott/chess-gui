#pragma once

#include "kiebitz/core/result.hpp"
#include "kiebitz/training/session.hpp"

#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace kiebitz::training {

/// A day as a number: days since 1 January 1970 (the app counts them in local time).
using Day = int;

/// Seconds trained on a day, by part (in kAllParts order).
using DaySeconds = std::array<int, kParts>;

/// Days in a row with some training, up to `today`. A day without any yet doesn't break it
/// until it's over: the streak then counts back from yesterday.
[[nodiscard]] int streak(const std::set<Day>& days, Day today);
/// The longest run of days in a row.
[[nodiscard]] int bestStreak(const std::set<Day>& days);

/// The time trained, per day and part, kept on this Mac (SQLite).
class Log {
public:
    [[nodiscard]] static core::Result<std::unique_ptr<Log>> open(const std::string& path);
    ~Log();
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;
    Log(Log&&) = delete;
    Log& operator=(Log&&) = delete;

    /// Adds time to a day's part.
    void add(Day day, Part part, int seconds);
    /// Each day from `from` on with some time.
    [[nodiscard]] std::map<Day, DaySeconds> days(Day from) const;
    /// Every day with some time (for the streaks).
    [[nodiscard]] std::set<Day> trainedDays() const;

private:
    struct Impl;
    explicit Log(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace kiebitz::training
