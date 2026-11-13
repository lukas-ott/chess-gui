#pragma once

#include "kiebitz/core/result.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kiebitz::puzzles {

/// One puzzle tried.
struct Attempt {
    std::string source;      ///< "lichess", or a puzzle file's path
    std::string puzzle;      ///< Lichess's id, or the game's key in its file
    bool solved = false;     ///< finished (with or without help)
    bool clean = false;      ///< solved without a wrong move, a hint or the solution
    std::int64_t millis = 0; ///< time taken (-1: the clock was stopped)
    int rating = 0;          ///< the puzzle's (0: unknown)
    std::int64_t when = 0;   ///< seconds since 1970
};

/// What the attempts of a source add up to.
struct Totals {
    int attempts = 0;
    int solved = 0; ///< finished, with or without help
    int clean = 0;  ///< solved without help
    int streak = 0; ///< clean ones in a row, the latest last
    int bestStreak = 0;
    std::int64_t averageMillis = 0; ///< of the timed ones
};

/// The puzzles you tried, kept on this Mac (SQLite). No rating: counts, streaks and times.
class History {
public:
    [[nodiscard]] static core::Result<std::unique_ptr<History>> open(const std::string& path);
    ~History();
    History(const History&) = delete;
    History& operator=(const History&) = delete;
    History(History&&) = delete;
    History& operator=(History&&) = delete;

    void record(const Attempt& attempt);
    /// The latest attempts of a source, newest first.
    [[nodiscard]] std::vector<Attempt> recent(const std::string& source, int limit) const;
    [[nodiscard]] Totals totals(const std::string& source) const;
    /// For a puzzle file: each puzzle's latest result (true: clean), by puzzle.
    [[nodiscard]] std::map<std::string, bool> latest(const std::string& source) const;
    /// Forgets a source's attempts.
    void clear(const std::string& source);

private:
    struct Impl;
    explicit History(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace kiebitz::puzzles
