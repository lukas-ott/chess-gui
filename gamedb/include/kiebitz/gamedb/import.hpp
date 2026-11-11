#pragma once

#include "kiebitz/gamedb/database.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace kiebitz::gamedb {

struct ImportProgress {
    std::uint64_t bytes = 0;   ///< read from the input so far
    std::uint64_t games = 0;   ///< stored so far
    std::uint64_t damaged = 0; ///< games kept only up to a broken move
    std::uint64_t skipped = 0; ///< variants and unreadable positions, not stored
};

struct ImportOptions {
    unsigned threads = 0; ///< games are read on this many threads; 0 = one per core
    /// Called now and then on a worker thread.
    std::function<void(const ImportProgress&)> progress;
    /// Set it to stop the import (it then returns `cancelled`).
    const std::atomic<bool>* cancel = nullptr;
};

struct ImportResult {
    ImportProgress counts;
    bool cancelled = false;
    std::string error; ///< empty unless the input couldn't be read
};

/// Reads PGN from `read` (which fills a buffer and returns how much it filled, 0 at the end)
/// and adds every game to `writer`, in the order of the input. Any size: the input is read
/// piece by piece and never held whole. The writer is not finished.
ImportResult importPgn(DatabaseWriter& writer,
                       const std::function<std::size_t(char* buffer, std::size_t size)>& read,
                       const ImportOptions& options = {});

} // namespace kiebitz::gamedb
