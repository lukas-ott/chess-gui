#include "kiebitz/library/index_cache.hpp"

#include "kiebitz/sql/sqlite.hpp"

#include <cstdio>
#include <mutex>
#include <sqlite3.h>
#include <unordered_set>
#include <utility>

namespace kiebitz::library {
namespace {

/// Bumped whenever the encoding or the tables change; an older cache is then emptied.
constexpr const char* kFormat = "1";

// ---- Encoding: unsigned LEB128 numbers and length-prefixed strings ----------------------------

void putNumber(std::string& out, std::uint64_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<char>((value & 0x7FU) | 0x80U));
        value >>= 7U;
    }
    out.push_back(static_cast<char>(value));
}

void putString(std::string& out, std::string_view text) {
    putNumber(out, text.size());
    out.append(text);
}

class Reader {
public:
    explicit Reader(std::string_view bytes) : m_bytes(bytes) {}

    bool number(std::uint64_t& value) {
        value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (m_pos >= m_bytes.size()) {
                return false;
            }
            const auto byte = static_cast<unsigned char>(m_bytes[m_pos++]);
            value |= static_cast<std::uint64_t>(byte & 0x7FU) << static_cast<unsigned>(shift);
            if ((byte & 0x80U) == 0) {
                return true;
            }
        }
        return false; // longer than any 64-bit number
    }

    bool string(std::string& text) {
        std::uint64_t length = 0;
        if (!number(length) || length > m_bytes.size() - m_pos) {
            return false;
        }
        text.assign(m_bytes.substr(m_pos, length));
        m_pos += length;
        return true;
    }

    [[nodiscard]] std::size_t left() const { return m_bytes.size() - m_pos; }

private:
    std::string_view m_bytes;
    std::size_t m_pos = 0;
};

// ---- SQLite helpers ---------------------------------------------------------------------------

using sql::bindText;
using sql::columnText;
using sql::exec;
using sql::Statement;

} // namespace

std::string encodeIndex(std::span<const pgn::GameEntry> games) {
    std::string out;
    out.reserve(games.size() * 96);
    putNumber(out, games.size());
    for (const auto& game : games) {
        putNumber(out, game.offset);
        putNumber(out, game.length);
        putNumber(out, game.headers.size());
        for (const auto& [name, value] : game.headers) {
            putString(out, name);
            putString(out, value);
        }
    }
    return out;
}

std::optional<std::vector<pgn::GameEntry>> decodeIndex(std::string_view bytes) {
    Reader in(bytes);
    std::uint64_t count = 0;
    // Every game takes at least 3 bytes, so a count larger than that is damage, not data.
    if (!in.number(count) || count > in.left() / 3) {
        return std::nullopt;
    }
    std::vector<pgn::GameEntry> games(count);
    for (auto& game : games) {
        std::uint64_t offset = 0;
        std::uint64_t length = 0;
        std::uint64_t headers = 0;
        if (!in.number(offset) || !in.number(length) || !in.number(headers) ||
            headers > in.left() / 2) {
            return std::nullopt;
        }
        game.offset = offset;
        game.length = length;
        game.headers.resize(headers);
        for (auto& [name, value] : game.headers) {
            if (!in.string(name) || !in.string(value)) {
                return std::nullopt;
            }
        }
    }
    if (in.left() != 0) {
        return std::nullopt;
    }
    return games;
}

// ---- The cache --------------------------------------------------------------------------------

struct IndexCache::Impl {
    sqlite3* db = nullptr;
    mutable std::mutex mutex; // one connection, shared by the scan's worker threads

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() { sqlite3_close(db); }
};

namespace {

/// Opens and prepares the database; an error message if that fails.
std::string setUp(sqlite3*& db, const std::string& path) {
    if (sqlite3_open_v2(path.c_str(), &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        return sql::errorOf(db);
    }
    sqlite3_busy_timeout(db, 2000); // another Kiebitz may be writing for a moment
    // WAL: reads never wait for a write. NORMAL: a crash may lose the last write, never the file.
    if (!exec(db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;") ||
        !exec(db, "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)")) {
        return sqlite3_errmsg(db);
    }
    std::string format;
    {
        Statement read;
        if (sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key = 'format'", -1, &read.handle,
                               nullptr) != SQLITE_OK) {
            return sqlite3_errmsg(db);
        }
        if (sqlite3_step(read.handle) == SQLITE_ROW) {
            format = columnText(read.handle, 0);
        }
    }
    if (format != kFormat) {
        const std::string reset = std::string("DROP TABLE IF EXISTS files;"
                                              "INSERT OR REPLACE INTO meta VALUES ('format', '") +
                                  kFormat + "');";
        if (!exec(db, reset.c_str())) {
            return sqlite3_errmsg(db);
        }
    }
    if (!exec(db, "CREATE TABLE IF NOT EXISTS files (path TEXT PRIMARY KEY, size INTEGER NOT "
                  "NULL, modified INTEGER NOT NULL, games BLOB NOT NULL)")) {
        return sqlite3_errmsg(db);
    }
    return {};
}

} // namespace

core::Result<std::unique_ptr<IndexCache>> IndexCache::open(const std::string& path) {
    auto impl = std::make_unique<Impl>();
    std::string error = setUp(impl->db, path);
    if (!error.empty()) {
        // A damaged file is only a cache: start a new one.
        sqlite3_close(impl->db);
        impl->db = nullptr;
        for (const char* suffix : {"", "-wal", "-shm"}) {
            std::remove((path + suffix).c_str());
        }
        error = setUp(impl->db, path);
        if (!error.empty()) {
            return core::Error{"Couldn't open the library cache: " + error};
        }
    }
    return std::unique_ptr<IndexCache>(new IndexCache(std::move(impl)));
}

IndexCache::IndexCache(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

IndexCache::~IndexCache() = default;

std::optional<std::vector<pgn::GameEntry>> IndexCache::find(std::string_view path,
                                                            FileStamp stamp) const {
    const std::scoped_lock lock(m_impl->mutex);
    Statement query;
    if (sqlite3_prepare_v2(m_impl->db, "SELECT size, modified, games FROM files WHERE path = ?", -1,
                           &query.handle, nullptr) != SQLITE_OK ||
        bindText(query.handle, 1, path) != SQLITE_OK || sqlite3_step(query.handle) != SQLITE_ROW) {
        return std::nullopt;
    }
    if (sqlite3_column_int64(query.handle, 0) != stamp.size ||
        sqlite3_column_int64(query.handle, 1) != stamp.modifiedMs) {
        return std::nullopt; // the file changed since
    }
    const auto* blob = static_cast<const char*>(sqlite3_column_blob(query.handle, 2));
    const auto bytes = static_cast<std::size_t>(sqlite3_column_bytes(query.handle, 2));
    return decodeIndex(std::string_view(blob == nullptr ? "" : blob, bytes));
}

void IndexCache::store(std::string_view path, FileStamp stamp,
                       std::span<const pgn::GameEntry> games) {
    const std::string bytes = encodeIndex(games);
    const std::scoped_lock lock(m_impl->mutex);
    Statement insert;
    if (sqlite3_prepare_v2(m_impl->db, "INSERT OR REPLACE INTO files VALUES (?, ?, ?, ?)", -1,
                           &insert.handle, nullptr) != SQLITE_OK) {
        return;
    }
    bindText(insert.handle, 1, path);
    sqlite3_bind_int64(insert.handle, 2, stamp.size);
    sqlite3_bind_int64(insert.handle, 3, stamp.modifiedMs);
    sqlite3_bind_blob(insert.handle, 4, bytes.data(), static_cast<int>(bytes.size()),
                      SQLITE_STATIC);
    sqlite3_step(insert.handle); // a failed write only means the file is read again next time
}

void IndexCache::forgetOthers(std::string_view folder, std::span<const std::string> keep) {
    const std::unordered_set<std::string_view> kept(keep.begin(), keep.end());
    std::string prefix(folder);
    if (!prefix.ends_with('/')) {
        prefix.push_back('/');
    }
    const std::scoped_lock lock(m_impl->mutex);
    std::vector<std::string> gone;
    {
        Statement query;
        if (sqlite3_prepare_v2(m_impl->db, "SELECT path FROM files", -1, &query.handle, nullptr) !=
            SQLITE_OK) {
            return;
        }
        while (sqlite3_step(query.handle) == SQLITE_ROW) {
            std::string path = columnText(query.handle, 0);
            if (path.starts_with(prefix) && !kept.contains(path)) {
                gone.push_back(std::move(path));
            }
        }
    }
    if (gone.empty()) {
        return;
    }
    exec(m_impl->db, "BEGIN");
    Statement remove;
    if (sqlite3_prepare_v2(m_impl->db, "DELETE FROM files WHERE path = ?", -1, &remove.handle,
                           nullptr) == SQLITE_OK) {
        for (const auto& path : gone) {
            bindText(remove.handle, 1, path);
            sqlite3_step(remove.handle);
            sqlite3_reset(remove.handle);
        }
    }
    exec(m_impl->db, "COMMIT");
}

std::size_t IndexCache::size() const {
    const std::scoped_lock lock(m_impl->mutex);
    Statement query;
    if (sqlite3_prepare_v2(m_impl->db, "SELECT COUNT(*) FROM files", -1, &query.handle, nullptr) !=
            SQLITE_OK ||
        sqlite3_step(query.handle) != SQLITE_ROW) {
        return 0;
    }
    return static_cast<std::size_t>(sqlite3_column_int64(query.handle, 0));
}

} // namespace kiebitz::library
