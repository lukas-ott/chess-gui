#pragma once

#include "kiebitz/core/result.hpp"
#include "kiebitz/core/types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>

/// Light practice statistics, kept on this Mac (not in the PGN files): for each position where
/// you were asked for your move, how often, how often you got it wrong, and how many right
/// answers in a row since the last mistake. Keyed by position (and your side), so they belong
/// to the position, not to a file: renaming or splitting files keeps them, and a position in
/// two files is one entry. No scheduling; it feeds "practise my mistakes".
namespace kiebitz::library {

struct PositionStats {
    int tries = 0;
    int mistakes = 0;
    int streak = 0; ///< right answers in a row since the last mistake
};

class TrainingStats {
public:
    /// Opens (or creates) the database at `path`.
    [[nodiscard]] static core::Result<std::unique_ptr<TrainingStats>> open(const std::string& path);
    ~TrainingStats();
    TrainingStats(const TrainingStats&) = delete;
    TrainingStats& operator=(const TrainingStats&) = delete;
    TrainingStats(TrainingStats&&) = delete;
    TrainingStats& operator=(TrainingStats&&) = delete;

    /// Your first answer at a position (with `side` to move): right or wrong.
    void record(core::Color side, std::uint64_t position, bool right);
    [[nodiscard]] PositionStats get(core::Color side, std::uint64_t position) const;

    /// A right answer this many times in a row after a mistake, and it's no longer one.
    static constexpr int kSettled = 2;
    /// Positions you got wrong and haven't answered right kSettled times in a row since.
    [[nodiscard]] std::unordered_set<std::uint64_t> mistakes(core::Color side) const;

private:
    struct Impl;
    explicit TrainingStats(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace kiebitz::library
