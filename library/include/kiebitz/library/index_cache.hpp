#pragma once

#include "kiebitz/core/result.hpp"
#include "kiebitz/pgn/pgn.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// The library's cache: for every PGN file it has seen, the game index (where each game starts
/// and ends, and its headers). With it the Library lists thousands of games at startup without
/// reading a single file; only files that are new or changed since are read and indexed again.
namespace kiebitz::library {

/// What identifies one version of a file: if either changes, the cached index is stale.
struct FileStamp {
    std::int64_t size = 0;
    std::int64_t modifiedMs = 0; ///< last modification, in ms since the epoch

    bool operator==(const FileStamp&) const = default;
};

/// The game index as a compact byte string, and back. decodeIndex fails (nullopt) on anything
/// that isn't a complete, valid encoding, so a damaged cache entry is just re-read.
[[nodiscard]] std::string encodeIndex(std::span<const pgn::GameEntry> games);
[[nodiscard]] std::optional<std::vector<pgn::GameEntry>> decodeIndex(std::string_view bytes);

/// The cache, stored in one SQLite file. Safe to use from several threads at once.
class IndexCache {
public:
    /// Opens the cache at `path`, creating it if needed. A cache written by another version of
    /// the format is emptied, never misread.
    [[nodiscard]] static core::Result<std::unique_ptr<IndexCache>> open(const std::string& path);

    ~IndexCache();
    IndexCache(const IndexCache&) = delete;
    IndexCache& operator=(const IndexCache&) = delete;
    IndexCache(IndexCache&&) = delete;
    IndexCache& operator=(IndexCache&&) = delete;

    /// The index stored for this file, if it was stored for exactly this version of it.
    [[nodiscard]] std::optional<std::vector<pgn::GameEntry>> find(std::string_view path,
                                                                  FileStamp stamp) const;
    /// Stores (or replaces) the index of one file.
    void store(std::string_view path, FileStamp stamp, std::span<const pgn::GameEntry> games);
    /// Forgets every file under `folder` that is not in `keep` (they were deleted or moved).
    void forgetOthers(std::string_view folder, std::span<const std::string> keep);
    /// How many files the cache knows (for tests).
    [[nodiscard]] std::size_t size() const;

private:
    struct Impl;
    explicit IndexCache(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace kiebitz::library
