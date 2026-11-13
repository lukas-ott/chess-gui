#pragma once

#include "kiebitz/puzzles/puzzle.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace kiebitz::puzzles {

/// The Lichess puzzle database, imported into one compact file: millions of puzzles sorted by
/// rating, so a puzzle in a rating range (and of a theme) is found at once. The file is mapped,
/// not read: opening it takes no time, whatever its size.
class PuzzleDb {
public:
    /// Null (and `error` set) if the file is missing or not a puzzle file.
    [[nodiscard]] static std::unique_ptr<PuzzleDb> open(const std::string& path,
                                                        std::string* error = nullptr);
    ~PuzzleDb();
    PuzzleDb(const PuzzleDb&) = delete;
    PuzzleDb& operator=(const PuzzleDb&) = delete;
    PuzzleDb(PuzzleDb&&) = delete;
    PuzzleDb& operator=(PuzzleDb&&) = delete;

    [[nodiscard]] std::size_t size() const noexcept { return m_count; }
    /// The themes the puzzles have ("fork", "mateIn2"), in the order of their ids.
    [[nodiscard]] const std::vector<std::string>& themes() const noexcept { return m_themes; }
    [[nodiscard]] int rating(std::size_t index) const;
    /// The lowest and highest rating (0, 0 when empty).
    [[nodiscard]] std::pair<int, int> ratingRange() const;
    /// How many puzzles are rated from `low` to `high` (both included).
    [[nodiscard]] std::size_t count(int low, int high) const;

    /// A random puzzle rated from `low` to `high`, of `theme` (an index into themes(); -1 for
    /// any). Nothing if there's none.
    [[nodiscard]] std::optional<std::size_t> pick(int low, int high, int theme,
                                                  std::mt19937_64& random) const;
    /// The puzzle, or nothing if it can't be read (a damaged record).
    [[nodiscard]] std::optional<Puzzle> puzzle(std::size_t index) const;
    /// Where a puzzle is, by its Lichess id (a linear search: for tests and links).
    [[nodiscard]] std::optional<std::size_t> find(std::string_view id) const;

private:
    PuzzleDb() = default;
    [[nodiscard]] bool hasTheme(std::size_t index, int theme) const;
    [[nodiscard]] std::pair<std::size_t, std::size_t> range(int low, int high) const;

    void* m_data = nullptr;
    std::size_t m_size = 0;
    std::size_t m_count = 0;
    std::vector<std::string> m_themes;
    const std::uint16_t* m_ratings = nullptr;
    const std::uint8_t* m_tags = nullptr; ///< kTagsPerPuzzle theme ids per puzzle, 0xFF unused
    const std::uint32_t* m_offsets = nullptr;
    const std::uint8_t* m_blob = nullptr;
    std::size_t m_blobSize = 0;
};

struct PuzzleImportProgress {
    std::uint64_t bytes = 0;   ///< read from the input so far (compressed)
    std::uint64_t puzzles = 0; ///< stored so far
};

struct PuzzleImportOptions {
    /// Called now and then (on the importing thread).
    std::function<void(const PuzzleImportProgress&)> progress;
    /// Set it to stop the import (it then returns `cancelled`).
    const std::atomic<bool>* cancel = nullptr;
};

struct PuzzleImportResult {
    std::uint64_t puzzles = 0;
    std::uint64_t skipped = 0; ///< lines that couldn't be read
    bool cancelled = false;
    std::string error; ///< empty unless it failed
};

/// Reads the Lichess puzzle CSV from `read` (which fills a buffer and returns how much it
/// filled, 0 at the end), zstd-compressed (lichess_db_puzzle.csv.zst) or plain, and writes the
/// puzzle file to `path`. Streams: the input is never held whole. `path` is only written when
/// it succeeds (a ".part" file meanwhile, removed on failure).
PuzzleImportResult importLichess(const std::function<std::size_t(char*, std::size_t)>& read,
                                 const std::string& path, const PuzzleImportOptions& options = {});

} // namespace kiebitz::puzzles
