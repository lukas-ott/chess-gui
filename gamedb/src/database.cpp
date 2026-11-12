#include "kiebitz/gamedb/database.hpp"

#include "codec.hpp"

#include <queue>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <sqlite3.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kiebitz::gamedb {
namespace {

constexpr const char* kFormat = "kiebitz-gamedb-1";

/// What attrs.bin holds for each game, in explorer order.
struct Attributes {
    std::uint16_t year = 0;
    std::uint8_t speed = 0; ///< a Speed
    std::uint8_t unused = 0;
    std::uint32_t white = 0; ///< name ids (0: none)
    std::uint32_t black = 0;
};
static_assert(sizeof(Attributes) == 12);

int yearOf(std::string_view date) {
    int year = 0;
    for (std::size_t i = 0; i < 4 && i < date.size() && date[i] >= '0' && date[i] <= '9'; ++i) {
        year = (year * 10) + (date[i] - '0');
    }
    return year;
}

// ---- Files -------------------------------------------------------------------------------------

std::string pathIn(const std::string& folder, const char* name) {
    return (std::filesystem::path(folder) / name).string();
}

/// A file mapped into memory, read-only. The system pages it in as it is read, so a database of
/// gigabytes opens at once and uses memory only for what is looked at.
class MappedFile {
public:
    MappedFile() = default;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept
        : m_data(std::exchange(other.m_data, nullptr)), m_size(std::exchange(other.m_size, 0)) {}
    MappedFile& operator=(MappedFile&& other) noexcept {
        std::swap(m_data, other.m_data);
        std::swap(m_size, other.m_size);
        return *this;
    }
    ~MappedFile() {
        if (m_data != nullptr) {
            munmap(m_data, m_size);
        }
    }

    /// False if the file can't be opened (an empty file maps to nothing, which is fine).
    bool open(const std::string& path) {
        const int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            return false;
        }
        struct stat status{};
        const bool sized = fstat(fd, &status) == 0;
        m_size = sized ? static_cast<std::size_t>(status.st_size) : 0;
        if (sized && m_size > 0) {
            void* data = mmap(nullptr, m_size, PROT_READ, MAP_PRIVATE, fd, 0);
            m_data = data == MAP_FAILED ? nullptr : data;
        }
        ::close(fd);
        return sized && (m_size == 0 || m_data != nullptr);
    }
    template <typename T> [[nodiscard]] const T* as() const {
        return static_cast<const T*>(m_data);
    }
    template <typename T> [[nodiscard]] std::size_t count() const { return m_size / sizeof(T); }

private:
    void* m_data = nullptr;
    std::size_t m_size = 0;
};

template <typename T> bool writeFile(const std::string& path, const std::vector<T>& values) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const bool written = values.empty() || std::fwrite(values.data(), sizeof(T), values.size(),
                                                       file) == values.size();
    return std::fclose(file) == 0 && written;
}

// ---- SQLite ------------------------------------------------------------------------------------

struct Statement {
    sqlite3_stmt* handle = nullptr;
    Statement() = default;
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &handle, nullptr) != SQLITE_OK) {
            handle = nullptr;
        }
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    Statement& operator=(Statement&& other) noexcept {
        std::swap(handle, other.handle);
        return *this;
    }
    ~Statement() { sqlite3_finalize(handle); }
};

bool exec(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

void bindText(sqlite3_stmt* statement, int index, std::string_view text) {
    if (text.empty()) {
        sqlite3_bind_null(statement, index);
    } else {
        sqlite3_bind_text(statement, index, text.data(), static_cast<int>(text.size()),
                          SQLITE_TRANSIENT);
    }
}

std::string columnText(sqlite3_stmt* statement, int column) {
    // As a blob: the same bytes, given as char (text comes as unsigned char).
    const auto* text = static_cast<const char*>(sqlite3_column_blob(statement, column));
    return text == nullptr
               ? std::string()
               : std::string(text,
                             static_cast<std::size_t>(sqlite3_column_bytes(statement, column)));
}

/// The columns every game query selects, in GameInfo's order.
constexpr const char* kGameColumns =
    "g.id, w.name, b.name, e.name, s.name, g.date, g.round, g.eco, g.white_elo, g.black_elo, "
    "g.result, g.plies";
/// Where they come from: the games, with the names of their players, event and site.
constexpr const char* kGameTables =
    " FROM games g LEFT JOIN names w ON w.id = g.white LEFT JOIN names b ON b.id = g.black "
    "LEFT JOIN names e ON e.id = g.event LEFT JOIN names s ON s.id = g.site";

GameInfo readGame(sqlite3_stmt* statement) {
    GameInfo info;
    info.id = sqlite3_column_int64(statement, 0);
    info.white = columnText(statement, 1);
    info.black = columnText(statement, 2);
    info.event = columnText(statement, 3);
    info.site = columnText(statement, 4);
    info.date = columnText(statement, 5);
    info.round = columnText(statement, 6);
    info.eco = columnText(statement, 7);
    info.whiteElo = sqlite3_column_int(statement, 8);
    info.blackElo = sqlite3_column_int(statement, 9);
    info.result = static_cast<GameResult>(sqlite3_column_int(statement, 10));
    info.plies = sqlite3_column_int(statement, 11);
    return info;
}

std::vector<GameInfo> readGames(sqlite3_stmt* statement) {
    std::vector<GameInfo> games;
    while (statement != nullptr && sqlite3_step(statement) == SQLITE_ROW) {
        games.push_back(readGame(statement));
    }
    return games;
}

std::string_view resultText(GameResult result) {
    switch (result) {
    case GameResult::WhiteWins:
        return "1-0";
    case GameResult::BlackWins:
        return "0-1";
    case GameResult::Draw:
        return "1/2-1/2";
    case GameResult::Unknown:
        break;
    }
    return "*";
}

/// The first 8 bytes of a game's moves as one number, for sorting: comparing these numbers
/// orders games the same way as comparing their bytes (a missing byte counts as 0, and a
/// shorter game that is a prefix of a longer one still sorts first, by the full comparison).
std::uint64_t sortKey(std::string_view moves) {
    std::uint64_t key = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        key = (key << 8U) | (i < moves.size() ? static_cast<std::uint8_t>(moves[i]) : 0U);
    }
    return key;
}

} // namespace

// ---- Writer ------------------------------------------------------------------------------------

struct DatabaseWriter::Impl {
    std::string folder;
    DatabaseInfo info;
    sqlite3* db = nullptr;
    Statement insertGame;
    Statement insertName;
    std::unordered_map<std::string, std::int64_t> names;
    std::unordered_set<std::int64_t> players;
    std::uint64_t online = 0; ///< games whose site is lichess.org or chess.com
    std::FILE* moves = nullptr;
    std::vector<std::uint64_t> offsets{0};
    std::vector<std::uint16_t> elo;
    std::vector<std::uint16_t> whiteElo;
    std::vector<Attributes> attributes; ///< by game, in the order added
    std::vector<std::uint16_t> blackElo;
    std::vector<std::uint8_t> results;
    std::vector<std::uint8_t> standard; ///< 1: from the usual start position

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() {
        insertGame = Statement();
        insertName = Statement();
        if (moves != nullptr) {
            std::fclose(moves);
        }
        sqlite3_close(db);
    }

    std::int64_t nameId(const std::string& name) {
        if (name.empty()) {
            return 0;
        }
        const auto found = names.find(name);
        if (found != names.end()) {
            return found->second;
        }
        const auto id = static_cast<std::int64_t>(names.size()) + 1;
        names.emplace(name, id);
        sqlite3_bind_int64(insertName.handle, 1, id);
        bindText(insertName.handle, 2, name);
        sqlite3_step(insertName.handle);
        sqlite3_reset(insertName.handle);
        return id;
    }
};

DatabaseWriter::DatabaseWriter(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
DatabaseWriter::~DatabaseWriter() = default;

core::Result<std::unique_ptr<DatabaseWriter>> DatabaseWriter::create(const std::string& folder,
                                                                     DatabaseInfo info) {
    auto impl = std::make_unique<Impl>();
    impl->folder = folder;
    impl->info = std::move(info);
    const std::string path = pathIn(folder, "games.sqlite");
    if (sqlite3_open_v2(path.c_str(), &impl->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
        return core::Error{"Couldn't create the database: " +
                           std::string(impl->db != nullptr ? sqlite3_errmsg(impl->db) : "")};
    }
    // While importing nothing else reads the file: no journal, writes in big steps.
    if (!exec(impl->db,
              "PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF; PRAGMA cache_size=-262144;"
              "CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
              "CREATE TABLE names (id INTEGER PRIMARY KEY, name TEXT NOT NULL);"
              "CREATE TABLE games (id INTEGER PRIMARY KEY, white INTEGER, black INTEGER, "
              "white_elo INTEGER, black_elo INTEGER, result INTEGER, date TEXT, event INTEGER, "
              "site INTEGER, round TEXT, eco TEXT, plies INTEGER, fen TEXT);"
              "BEGIN")) {
        return core::Error{"Couldn't create the database: " +
                           std::string(sqlite3_errmsg(impl->db))};
    }
    impl->insertGame = Statement(impl->db, "INSERT INTO games (white, black, white_elo, "
                                           "black_elo, result, date, event, site, round, eco, "
                                           "plies, fen) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
    impl->insertName = Statement(impl->db, "INSERT INTO names VALUES (?, ?)");
    impl->moves = std::fopen(pathIn(folder, "moves.bin").c_str(), "wb");
    if (impl->insertGame.handle == nullptr || impl->insertName.handle == nullptr ||
        impl->moves == nullptr) {
        return core::Error{"Couldn't create the database's files."};
    }
    return std::unique_ptr<DatabaseWriter>(new DatabaseWriter(std::move(impl)));
}

void DatabaseWriter::add(const ScannedGame& game) {
    if (game.skipped) {
        return;
    }
    Impl& d = *m_impl;
    sqlite3_stmt* insert = d.insertGame.handle;
    const std::int64_t white = d.nameId(game.white);
    const std::int64_t black = d.nameId(game.black);
    for (const std::int64_t player : {white, black}) {
        if (player != 0) {
            d.players.insert(player);
        }
    }
    if (game.site.find("lichess.org") != std::string::npos ||
        game.site.find("chess.com") != std::string::npos ||
        game.site.find("Chess.com") != std::string::npos) {
        ++d.online;
    }
    const auto bindId = [insert](int index, std::int64_t id) {
        if (id == 0) {
            sqlite3_bind_null(insert, index);
        } else {
            sqlite3_bind_int64(insert, index, id);
        }
    };
    bindId(1, white);
    bindId(2, black);
    bindId(3, game.whiteElo);
    bindId(4, game.blackElo);
    sqlite3_bind_int(insert, 5, static_cast<int>(game.result));
    bindText(insert, 6, game.date);
    bindId(7, d.nameId(game.event));
    bindId(8, d.nameId(game.site));
    bindText(insert, 9, game.round);
    bindText(insert, 10, game.eco);
    sqlite3_bind_int(insert, 11, static_cast<int>(game.moves.size()));
    bindText(insert, 12, game.fen);
    sqlite3_step(insert);
    sqlite3_reset(insert);

    std::fwrite(game.moves.data(), 1, game.moves.size(), d.moves);
    d.offsets.push_back(d.offsets.back() + game.moves.size());
    const int rated = (game.whiteElo > 0 ? 1 : 0) + (game.blackElo > 0 ? 1 : 0);
    d.elo.push_back(
        static_cast<std::uint16_t>(rated == 0 ? 0 : (game.whiteElo + game.blackElo) / rated));
    d.whiteElo.push_back(static_cast<std::uint16_t>(std::clamp(game.whiteElo, 0, 65535)));
    d.attributes.push_back({static_cast<std::uint16_t>(std::clamp(yearOf(game.date), 0, 65535)),
                            static_cast<std::uint8_t>(game.speed), 0,
                            static_cast<std::uint32_t>(white), static_cast<std::uint32_t>(black)});
    d.blackElo.push_back(static_cast<std::uint16_t>(std::clamp(game.blackElo, 0, 65535)));
    d.results.push_back(static_cast<std::uint8_t>(game.result));
    d.standard.push_back(game.fen.empty() ? 1 : 0);
    // Commit now and then, so the transaction never grows too big.
    if (d.results.size() % 200000 == 0) {
        exec(d.db, "COMMIT; BEGIN");
    }
}

std::uint64_t DatabaseWriter::count() const {
    return m_impl->results.size();
}

std::string DatabaseWriter::finish(const std::function<void(double)>& progress) {
    Impl& d = *m_impl;
    const auto report = [&](double value) {
        if (progress) {
            progress(value);
        }
    };
    if (std::fclose(d.moves) != 0) {
        d.moves = nullptr;
        return "Couldn't write the games' moves (is the disk full?).";
    }
    d.moves = nullptr;
    if (!writeFile(pathIn(d.folder, "offsets.bin"), d.offsets) ||
        !writeFile(pathIn(d.folder, "elo.bin"), d.elo)) {
        return "Couldn't write the database (is the disk full?).";
    }
    report(0.05);

    // The opening order: games from the start position, sorted by their moves.
    {
        MappedFile moves;
        if (!moves.open(pathIn(d.folder, "moves.bin"))) {
            return "Couldn't read back the games' moves.";
        }
        const char* bytes = moves.as<char>();
        const auto movesOf = [&](std::uint32_t game) {
            return std::string_view(bytes + d.offsets[game], d.offsets[game + 1] - d.offsets[game]);
        };
        std::vector<std::uint32_t> order;
        order.reserve(d.standard.size());
        for (std::uint32_t game = 0; game < d.standard.size(); ++game) {
            if (d.standard[game] != 0) {
                order.push_back(game);
            }
        }
        std::vector<std::uint64_t> keys(d.standard.size());
        for (const std::uint32_t game : order) {
            keys[game] = sortKey(movesOf(game));
        }
        std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
            if (keys[a] != keys[b]) {
                return keys[a] < keys[b];
            }
            const int compared = movesOf(a).compare(movesOf(b));
            return compared != 0 ? compared < 0 : a < b;
        });
        report(0.5);
        // Running totals of White wins, draws and Black wins along that order.
        std::vector<std::uint32_t> sums(3 * (order.size() + 1), 0);
        for (std::size_t i = 0; i < order.size(); ++i) {
            for (std::size_t k = 0; k < 3; ++k) {
                sums[(3 * (i + 1)) + k] = sums[(3 * i) + k];
            }
            const auto result = static_cast<GameResult>(d.results[order[i]]);
            if (result == GameResult::WhiteWins) {
                ++sums[(3 * (i + 1)) + 0];
            } else if (result == GameResult::Draw) {
                ++sums[(3 * (i + 1)) + 1];
            } else if (result == GameResult::BlackWins) {
                ++sums[(3 * (i + 1)) + 2];
            }
        }
        // Both players' ratings, in the same order, for the explorer's averages.
        std::vector<std::uint16_t> ratings;
        ratings.reserve(2 * order.size());
        for (const std::uint32_t game : order) {
            ratings.push_back(d.whiteElo[game]);
            ratings.push_back(d.blackElo[game]);
        }
        std::vector<Attributes> attributes;
        attributes.reserve(order.size());
        for (const std::uint32_t game : order) {
            attributes.push_back(d.attributes[game]);
        }
        if (!writeFile(pathIn(d.folder, "order.bin"), order) ||
            !writeFile(pathIn(d.folder, "sums.bin"), sums) ||
            !writeFile(pathIn(d.folder, "ratings.bin"), ratings) ||
            !writeFile(pathIn(d.folder, "attrs.bin"), attributes)) {
            return "Couldn't write the database (is the disk full?).";
        }
    }
    report(0.6);

    // The search indexes, and what the database is.
    d.insertGame = Statement();
    d.insertName = Statement();
    auto meta = std::make_unique<Statement>(d.db, "INSERT OR REPLACE INTO meta VALUES (?, ?)");
    const auto setMeta = [&](const char* key, const std::string& value) {
        sqlite3_bind_text(meta->handle, 1, key, -1, SQLITE_STATIC);
        sqlite3_bind_text(meta->handle, 2, value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(meta->handle);
        sqlite3_reset(meta->handle);
    };
    setMeta("format", kFormat);
    setMeta("name", d.info.name);
    setMeta("source", d.info.source);
    setMeta("games", std::to_string(d.results.size()));
    setMeta("players", std::to_string(d.players.size()));
    setMeta("online", 2 * d.online > d.results.size() ? "1" : "0");
    meta.reset(); // finished before the commit
    if (!exec(d.db, "COMMIT")) {
        return "Couldn't write the database: " + std::string(sqlite3_errmsg(d.db));
    }
    const std::array indexes{
        "CREATE INDEX games_white ON games (white)", "CREATE INDEX games_black ON games (black)",
        "CREATE INDEX games_event ON games (event)", "CREATE INDEX games_site ON games (site)",
        "CREATE INDEX games_eco ON games (eco)",     "CREATE INDEX games_date ON games (date)"};
    double done = 0.6;
    for (const char* index : indexes) {
        if (!exec(d.db, index)) {
            return "Couldn't index the database: " + std::string(sqlite3_errmsg(d.db));
        }
        done += 0.4 / std::size(indexes);
        report(done);
    }
    return {};
}

// ---- Reader ------------------------------------------------------------------------------------

struct Database::Impl {
    DatabaseInfo info;
    sqlite3* db = nullptr;
    MappedFile moves;
    MappedFile offsets;
    MappedFile order;
    MappedFile sums;
    MappedFile elo;
    MappedFile ratings;    ///< optional: databases made before it don't have it
    MappedFile attributes; ///< optional too

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() { sqlite3_close(db); }

    [[nodiscard]] std::size_t games() const {
        const std::size_t count = offsets.count<std::uint64_t>();
        return count > 0 ? count - 1 : 0;
    }
    /// A game's moves; `game` counts from 0 (its id - 1).
    [[nodiscard]] std::string_view movesOf(std::uint32_t game) const {
        const auto* at = offsets.as<std::uint64_t>();
        return {moves.as<char>() + at[game], static_cast<std::size_t>(at[game + 1] - at[game])};
    }
    /// The range of order.bin whose games start with `prefix`.
    [[nodiscard]] std::pair<std::size_t, std::size_t> range(std::string_view prefix) const {
        const auto* first = order.as<std::uint32_t>();
        const auto* last = first + order.count<std::uint32_t>();
        const auto head = [&](std::uint32_t game) {
            return movesOf(game).substr(0, prefix.size());
        };
        const auto* lo = std::partition_point(
            first, last, [&](std::uint32_t game) { return head(game) < prefix; });
        const auto* hi = std::partition_point(
            lo, last, [&](std::uint32_t game) { return head(game) <= prefix; });
        return {static_cast<std::size_t>(lo - first), static_cast<std::size_t>(hi - first)};
    }
    /// White's and Black's average rating in [lo, hi) of order.bin (0: none known).
    [[nodiscard]] std::pair<int, int> averages(std::size_t lo, std::size_t hi) const {
        if (ratings.count<std::uint16_t>() < 2 * hi) {
            return {0, 0};
        }
        const auto* at = ratings.as<std::uint16_t>();
        std::uint64_t whiteSum = 0;
        std::uint64_t blackSum = 0;
        std::uint64_t whiteCount = 0;
        std::uint64_t blackCount = 0;
        for (std::size_t i = lo; i < hi; ++i) {
            const std::uint16_t white = at[2 * i];
            const std::uint16_t black = at[(2 * i) + 1];
            whiteSum += white;
            blackSum += black;
            whiteCount += white > 0 ? 1U : 0U;
            blackCount += black > 0 ? 1U : 0U;
        }
        return {whiteCount > 0 ? static_cast<int>(whiteSum / whiteCount) : 0,
                blackCount > 0 ? static_cast<int>(blackSum / blackCount) : 0};
    }
    /// The result of the game at position `i` of order.bin (from the running totals).
    [[nodiscard]] GameResult resultAt(std::size_t i) const {
        const auto* at = sums.as<std::uint32_t>();
        if (at[(3 * (i + 1)) + 0] != at[(3 * i) + 0]) {
            return GameResult::WhiteWins;
        }
        if (at[(3 * (i + 1)) + 1] != at[(3 * i) + 1]) {
            return GameResult::Draw;
        }
        if (at[(3 * (i + 1)) + 2] != at[(3 * i) + 2]) {
            return GameResult::BlackWins;
        }
        return GameResult::Unknown;
    }

    /// A filter, ready to test games by their position in order.bin.
    struct Matcher {
        const Impl* impl = nullptr;
        ExploreFilter filter;
        std::unordered_set<std::uint32_t> players; ///< name ids
        std::unordered_set<std::uint32_t> whites;  ///< names that fit `filter.white`
        std::unordered_set<std::uint32_t> blacks;
        [[nodiscard]] bool operator()(std::size_t i) const {
            const auto* sorted = impl->order.as<std::uint32_t>();
            if (filter.minRating > 0 || filter.maxRating > 0) {
                const int rating = impl->elo.as<std::uint16_t>()[sorted[i]];
                if (rating == 0 || (filter.minRating > 0 && rating < filter.minRating) ||
                    (filter.maxRating > 0 && rating > filter.maxRating)) {
                    return false;
                }
            }
            if (impl->attributes.count<Attributes>() <= i) {
                return true; // an older database: only ratings can be filtered
            }
            const Attributes& a = impl->attributes.as<Attributes>()[i];
            if ((filter.fromYear > 0 && std::cmp_less(a.year, filter.fromYear)) ||
                (filter.toYear > 0 && (a.year == 0 || std::cmp_greater(a.year, filter.toYear)))) {
                return false;
            }
            if (filter.speeds != 0 && (filter.speeds & (1U << a.speed)) == 0) {
                return false;
            }
            if (!filter.white.empty() && !whites.contains(a.white)) {
                return false;
            }
            if (!filter.black.empty() && !blacks.contains(a.black)) {
                return false;
            }
            if (!filter.players.empty()) {
                const bool white = players.contains(a.white);
                const bool black = players.contains(a.black);
                switch (filter.side) {
                case ExploreFilter::Side::White:
                    return white;
                case ExploreFilter::Side::Black:
                    return black;
                case ExploreFilter::Side::Any:
                    return white || black;
                }
            }
            return true;
        }
    };
    [[nodiscard]] Matcher matcher(const ExploreFilter& filter) const {
        Matcher match{this, filter, {}, {}, {}};
        // Names containing the text (any case): their ids.
        const auto like = [this](const std::string& text, std::unordered_set<std::uint32_t>& ids) {
            std::string pattern = "%";
            for (const char c : text) {
                if (c == '%' || c == '_' || c == '\\') {
                    pattern += '\\';
                }
                pattern += c;
            }
            pattern += '%';
            const Statement query(db, "SELECT id FROM names WHERE name LIKE ? ESCAPE '\\'");
            sqlite3_bind_text(query.handle, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
            while (query.handle != nullptr && sqlite3_step(query.handle) == SQLITE_ROW) {
                ids.insert(static_cast<std::uint32_t>(sqlite3_column_int64(query.handle, 0)));
            }
        };
        if (!filter.white.empty()) {
            like(filter.white, match.whites);
        }
        if (!filter.black.empty()) {
            like(filter.black, match.blacks);
        }
        if (!filter.players.empty()) {
            std::string list;
            for (std::size_t i = 0; i < filter.players.size(); ++i) {
                list += i == 0 ? "?" : ",?";
            }
            const Statement query(
                db, ("SELECT id FROM names WHERE lower(name) IN (" + list + ")").c_str());
            for (std::size_t i = 0; i < filter.players.size(); ++i) {
                std::string lower = filter.players[i];
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                sqlite3_bind_text(query.handle, static_cast<int>(i) + 1, lower.c_str(), -1,
                                  SQLITE_TRANSIENT);
            }
            while (query.handle != nullptr && sqlite3_step(query.handle) == SQLITE_ROW) {
                match.players.insert(
                    static_cast<std::uint32_t>(sqlite3_column_int64(query.handle, 0)));
            }
        }
        return match;
    }

    /// What a filtered range holds: its games, results and both sides' rating sums.
    struct Tally {
        std::uint64_t games = 0;
        std::array<std::uint64_t, 3> results{};
        std::array<std::uint64_t, 4> ratings{}; ///< White's sum, count; Black's sum, count
        [[nodiscard]] std::pair<int, int> averages() const {
            return {ratings[1] > 0 ? static_cast<int>(ratings[0] / ratings[1]) : 0,
                    ratings[3] > 0 ? static_cast<int>(ratings[2] / ratings[3]) : 0};
        }
    };
    [[nodiscard]] Tally tally(std::size_t lo, std::size_t hi, const Matcher& match) const {
        Tally t;
        const bool rated = ratings.count<std::uint16_t>() >= 2 * hi;
        for (std::size_t i = lo; i < hi; ++i) {
            if (!match(i)) {
                continue;
            }
            ++t.games;
            switch (resultAt(i)) {
            case GameResult::WhiteWins:
                ++t.results[0];
                break;
            case GameResult::Draw:
                ++t.results[1];
                break;
            case GameResult::BlackWins:
                ++t.results[2];
                break;
            case GameResult::Unknown:
                break;
            }
            if (rated) {
                const auto* at = ratings.as<std::uint16_t>();
                for (std::size_t side = 0; side < 2; ++side) {
                    if (const std::uint16_t value = at[(2 * i) + side]; value > 0) {
                        t.ratings[2 * side] += value;
                        ++t.ratings[(2 * side) + 1];
                    }
                }
            }
        }
        return t;
    }

    /// White wins, draws and Black wins in [lo, hi) of order.bin.
    [[nodiscard]] std::array<std::uint64_t, 3> results(std::size_t lo, std::size_t hi) const {
        const auto* at = sums.as<std::uint32_t>();
        return {at[(3 * hi) + 0] - at[(3 * lo) + 0], at[(3 * hi) + 1] - at[(3 * lo) + 1],
                at[(3 * hi) + 2] - at[(3 * lo) + 2]};
    }
    /// The bytes of a line of moves from the start position, or nothing if one isn't legal.
    [[nodiscard]] static std::optional<std::string> prefixOf(std::span<const core::Move> line,
                                                             chess::Board& board) {
        std::string prefix;
        for (const core::Move& move : line) {
            const auto lib = detail::fromCore(board, move);
            const auto byte = lib ? detail::encode(board, *lib) : std::nullopt;
            if (!byte) {
                return std::nullopt;
            }
            prefix += static_cast<char>(*byte);
            board.makeMove(*lib);
        }
        return prefix;
    }
    [[nodiscard]] std::vector<GameInfo> byIds(const std::vector<GameId>& ids) const {
        if (ids.empty()) {
            return {};
        }
        std::string sql = std::string("SELECT ") + kGameColumns + kGameTables + " WHERE g.id IN (";
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (i > 0) {
                sql += ',';
            }
            sql += std::to_string(ids[i]);
        }
        sql += ')';
        const Statement query(db, sql.c_str());
        std::vector<GameInfo> found = readGames(query.handle);
        // In the order asked for.
        std::unordered_map<GameId, std::size_t> position;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            position.emplace(ids[i], i);
        }
        std::sort(found.begin(), found.end(), [&](const GameInfo& a, const GameInfo& b) {
            return position[a.id] < position[b.id];
        });
        return found;
    }
};

Database::Database(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
Database::~Database() = default;

core::Result<std::unique_ptr<Database>> Database::open(const std::string& folder) {
    auto impl = std::make_unique<Impl>();
    const std::string path = pathIn(folder, "games.sqlite");
    if (sqlite3_open_v2(path.c_str(), &impl->db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        return core::Error{"Couldn't open the database at " + folder};
    }
    {
        const Statement meta(impl->db, "SELECT key, value FROM meta");
        std::unordered_map<std::string, std::string> values;
        while (meta.handle != nullptr && sqlite3_step(meta.handle) == SQLITE_ROW) {
            values[columnText(meta.handle, 0)] = columnText(meta.handle, 1);
        }
        if (values["format"] != kFormat) {
            return core::Error{"This database was made by another version of Kiebitz, or its "
                               "import didn't finish."};
        }
        impl->info.name = values["name"];
        impl->info.source = values["source"];
        impl->info.games = std::stoull("0" + values["games"]);
        impl->info.players = std::stoull("0" + values["players"]);
        impl->info.online = values["online"] == "1"; // older databases: over the board
    }
    for (auto [file, name] :
         {std::pair{&impl->moves, "moves.bin"}, std::pair{&impl->offsets, "offsets.bin"},
          std::pair{&impl->order, "order.bin"}, std::pair{&impl->sums, "sums.bin"},
          std::pair{&impl->elo, "elo.bin"}}) {
        if (!file->open(pathIn(folder, name))) {
            return core::Error{std::string("The database is missing ") + name + "."};
        }
    }
    impl->ratings.open(pathIn(folder, "ratings.bin"));  // optional
    impl->attributes.open(pathIn(folder, "attrs.bin")); // optional
    return std::unique_ptr<Database>(new Database(std::move(impl)));
}

const DatabaseInfo& Database::info() const {
    return m_impl->info;
}

bool Database::hasRatings() const {
    return m_impl->ratings.count<std::uint16_t>() == 2 * m_impl->order.count<std::uint32_t>() &&
           m_impl->order.count<std::uint32_t>() > 0;
}

bool Database::hasAttributes() const {
    return m_impl->attributes.count<Attributes>() == m_impl->order.count<std::uint32_t>() &&
           m_impl->order.count<std::uint32_t>() > 0;
}

std::string_view upgrade(const std::string& folder) {
    MappedFile offsets;
    MappedFile order;
    if (!offsets.open(pathIn(folder, "offsets.bin")) || !order.open(pathIn(folder, "order.bin"))) {
        return "The database is incomplete.";
    }
    const std::size_t games =
        offsets.count<std::uint64_t>() > 0 ? offsets.count<std::uint64_t>() - 1 : 0;
    std::vector<std::uint16_t> white(games, 0);
    std::vector<std::uint16_t> black(games, 0);
    std::vector<Attributes> byGame(games);
    sqlite3* db = nullptr;
    const std::string path = pathIn(folder, "games.sqlite");
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return "Couldn't read the database.";
    }
    {
        const Statement query(db, "SELECT id, white_elo, black_elo, date, white, black FROM games");
        while (query.handle != nullptr && sqlite3_step(query.handle) == SQLITE_ROW) {
            const auto id = sqlite3_column_int64(query.handle, 0);
            if (id < 1 || std::cmp_greater(id, games)) {
                continue;
            }
            const auto index = static_cast<std::size_t>(id - 1);
            white[index] = static_cast<std::uint16_t>(
                std::clamp(sqlite3_column_int(query.handle, 1), 0, 65535));
            black[index] = static_cast<std::uint16_t>(
                std::clamp(sqlite3_column_int(query.handle, 2), 0, 65535));
            byGame[index].year = static_cast<std::uint16_t>(
                std::clamp(yearOf(columnText(query.handle, 3)), 0, 65535));
            byGame[index].white = static_cast<std::uint32_t>(sqlite3_column_int64(query.handle, 4));
            byGame[index].black = static_cast<std::uint32_t>(sqlite3_column_int64(query.handle, 5));
        }
    }
    sqlite3_close(db);
    std::vector<std::uint16_t> ratings;
    std::vector<Attributes> attributes;
    ratings.reserve(2 * order.count<std::uint32_t>());
    attributes.reserve(order.count<std::uint32_t>());
    const auto* sorted = order.as<std::uint32_t>();
    for (std::size_t i = 0; i < order.count<std::uint32_t>(); ++i) {
        const bool known = sorted[i] < games;
        ratings.push_back(known ? white[sorted[i]] : 0);
        ratings.push_back(known ? black[sorted[i]] : 0);
        attributes.push_back(known ? byGame[sorted[i]] : Attributes{});
    }
    // Written under other names and renamed, so a reader never sees half a file.
    for (const auto& [name, write] :
         {std::pair<const char*, std::function<bool(const std::string&)>>{
              "ratings.bin", [&](const std::string& to) { return writeFile(to, ratings); }},
          std::pair<const char*, std::function<bool(const std::string&)>>{
              "attrs.bin", [&](const std::string& to) { return writeFile(to, attributes); }}}) {
        const std::string target = pathIn(folder, name);
        const std::string part = target + ".part";
        if (!write(part) || std::rename(part.c_str(), target.c_str()) != 0) {
            std::remove(part.c_str());
            return "Couldn't upgrade the database (is the disk full?).";
        }
    }
    return {};
}

std::uint64_t Database::gameCount() const {
    return m_impl->games();
}

std::vector<GameInfo> Database::games(GameId first, std::size_t count) const {
    const Statement query(m_impl->db, (std::string("SELECT ") + kGameColumns + kGameTables +
                                       " WHERE g.id >= ? ORDER BY g.id LIMIT ?")
                                          .c_str());
    sqlite3_bind_int64(query.handle, 1, first);
    sqlite3_bind_int64(query.handle, 2, static_cast<sqlite3_int64>(count));
    return readGames(query.handle);
}

std::vector<GameInfo> Database::search(std::string_view text, std::size_t limit) const {
    if (text.empty()) {
        return games(1, limit);
    }
    // LIKE with % and _ taken literally.
    std::string pattern = "%";
    for (const char c : text) {
        if (c == '%' || c == '_' || c == '\\') {
            pattern += '\\';
        }
        pattern += c;
    }
    pattern += '%';
    const Statement query(
        m_impl->db, (std::string("WITH hit AS (SELECT id FROM names WHERE name LIKE ?1 ESCAPE "
                                 "'\\') SELECT ") +
                     kGameColumns + kGameTables +
                     " WHERE g.white IN hit OR g.black IN hit OR g.event IN hit OR g.site IN hit "
                     "OR g.eco = ?2 ORDER BY g.date DESC LIMIT ?3")
                        .c_str());
    bindText(query.handle, 1, pattern);
    bindText(query.handle, 2, text);
    sqlite3_bind_int64(query.handle, 3, static_cast<sqlite3_int64>(limit));
    return readGames(query.handle);
}

std::optional<core::Game> Database::game(GameId id) const {
    if (id < 1 || std::cmp_greater(id, m_impl->games())) {
        return std::nullopt;
    }
    const Statement query(m_impl->db, (std::string("SELECT ") + kGameColumns + ", g.fen" +
                                       kGameTables + " WHERE g.id = ?")
                                          .c_str());
    sqlite3_bind_int64(query.handle, 1, id);
    if (query.handle == nullptr || sqlite3_step(query.handle) != SQLITE_ROW) {
        return std::nullopt;
    }
    const GameInfo info = readGame(query.handle);
    const std::string fen = columnText(query.handle, 12);
    chess::Board board;
    core::Position start;
    if (!fen.empty()) {
        auto parsed = core::Position::fromFen(fen);
        if (!parsed.ok() || !board.setFen(fen)) {
            return std::nullopt;
        }
        start = *parsed;
    }
    core::Game game(start);
    const auto header = [&](const char* name, const std::string& value) {
        if (!value.empty()) {
            game.setHeader(name, value);
        }
    };
    header("Event", info.event);
    header("Site", info.site);
    header("Date", info.date);
    header("Round", info.round);
    header("White", info.white);
    header("Black", info.black);
    header("Result", std::string(resultText(info.result)));
    header("WhiteElo", info.whiteElo > 0 ? std::to_string(info.whiteElo) : "");
    header("BlackElo", info.blackElo > 0 ? std::to_string(info.blackElo) : "");
    header("ECO", info.eco);
    if (!fen.empty()) {
        header("SetUp", "1");
        header("FEN", fen);
    }
    core::NodeId node = core::Game::root();
    for (const char byte : m_impl->movesOf(static_cast<std::uint32_t>(id - 1))) {
        const auto move = detail::decode(board, static_cast<std::uint8_t>(byte));
        if (!move) {
            break;
        }
        node = game.addMove(node, detail::toCore(*move));
        board.makeMove(*move);
    }
    return game;
}

Record Database::record(std::span<const std::string> names) const {
    Record record;
    if (names.empty()) {
        return record;
    }
    // The players' ids (names are compared without case), then their results by colour.
    std::string list;
    for (std::size_t i = 0; i < names.size(); ++i) {
        list += i == 0 ? "?" : ",?";
    }
    for (const bool white : {true, false}) {
        const std::string sql =
            std::string("SELECT result, COUNT(*) FROM games WHERE ") + (white ? "white" : "black") +
            " IN (SELECT id FROM names WHERE lower(name) IN (" + list + ")) GROUP BY result";
        const Statement query(m_impl->db, sql.c_str());
        for (std::size_t i = 0; i < names.size(); ++i) {
            std::string lower = names[i];
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            sqlite3_bind_text(query.handle, static_cast<int>(i) + 1, lower.c_str(), -1,
                              SQLITE_TRANSIENT);
        }
        auto& counts = white ? record.asWhite : record.asBlack;
        while (query.handle != nullptr && sqlite3_step(query.handle) == SQLITE_ROW) {
            const auto result = static_cast<GameResult>(sqlite3_column_int(query.handle, 0));
            const auto count = static_cast<std::uint64_t>(sqlite3_column_int64(query.handle, 1));
            if (result == GameResult::Draw) {
                counts[1] += count;
            } else if (result == GameResult::WhiteWins) {
                counts[white ? 0 : 2] += count;
            } else if (result == GameResult::BlackWins) {
                counts[white ? 2 : 0] += count;
            }
        }
    }
    return record;
}

PlayerStats Database::playerStats(std::span<const std::string> names, std::uint8_t speeds) const {
    PlayerStats stats;
    if (names.empty() || !hasAttributes()) {
        stats.total = speeds == 0 ? record(names) : Record{};
        return stats;
    }
    ExploreFilter filter;
    filter.players.assign(names.begin(), names.end());
    const Impl::Matcher match = m_impl->matcher(filter);
    const auto* attributes = m_impl->attributes.as<Attributes>();
    for (std::size_t i = 0; i < m_impl->order.count<std::uint32_t>(); ++i) {
        const Attributes& a = attributes[i];
        if (speeds != 0 && (speeds & (1U << a.speed)) == 0) {
            continue;
        }
        const bool white = match.players.contains(a.white);
        const bool black = match.players.contains(a.black);
        if (!white && !black) {
            continue;
        }
        const GameResult result = m_impl->resultAt(i);
        if (result == GameResult::Unknown) {
            continue;
        }
        // From the player's side: 0 won, 1 drawn, 2 lost (a game against oneself counts once).
        std::size_t outcome = 1;
        if (result == GameResult::WhiteWins) {
            outcome = white ? 0 : 2;
        } else if (result == GameResult::BlackWins) {
            outcome = white ? 2 : 0;
        }
        Record& year = stats.years[a.year];
        auto& total = white ? stats.total.asWhite : stats.total.asBlack;
        auto& inYear = white ? year.asWhite : year.asBlack;
        ++total[outcome];
        ++inYear[outcome];
    }
    return stats;
}

Explored Database::explore(std::span<const core::Move> line, const ExploreFilter& filter) const {
    Explored explored;
    chess::Board board;
    const auto prefix = Impl::prefixOf(line, board);
    if (!prefix) {
        return explored;
    }
    const auto [lo, hi] = m_impl->range(*prefix);
    // Filtered: the games are counted one by one; otherwise the running totals do it.
    const bool filtered = filter.active();
    const Impl::Matcher match = filtered ? m_impl->matcher(filter) : Impl::Matcher{};
    if (filtered) {
        const Impl::Tally t = m_impl->tally(lo, hi, match);
        explored.games = t.games;
        explored.white = t.results[0];
        explored.draws = t.results[1];
        explored.black = t.results[2];
        std::tie(explored.whiteElo, explored.blackElo) = t.averages();
    } else {
        const auto total = m_impl->results(lo, hi);
        explored.games = hi - lo;
        explored.white = total[0];
        explored.draws = total[1];
        explored.black = total[2];
        std::tie(explored.whiteElo, explored.blackElo) = m_impl->averages(lo, hi);
    }
    // The next moves: games that go on are sorted by their next byte, so each move is a run.
    const auto* order = m_impl->order.as<std::uint32_t>();
    const std::size_t at = prefix->size();
    std::size_t i = lo;
    while (i < hi && m_impl->movesOf(order[i]).size() <= at) {
        ++i; // games that end here
    }
    while (i < hi) {
        const char byte = m_impl->movesOf(order[i])[at];
        const auto* end = std::partition_point(order + i, order + hi, [&](std::uint32_t game) {
            return m_impl->movesOf(game)[at] == byte;
        });
        const auto next = static_cast<std::size_t>(end - order);
        const auto move = detail::decode(board, static_cast<std::uint8_t>(byte));
        if (move && filtered) {
            const Impl::Tally t = m_impl->tally(i, next, match);
            const auto [whiteElo, blackElo] = t.averages();
            if (t.games > 0) {
                explored.moves.push_back(
                    {detail::toCore(*move), chess::uci::moveToSan(board, *move), t.games,
                     t.results[0], t.results[1], t.results[2], whiteElo, blackElo});
            }
        } else if (move) {
            const auto results = m_impl->results(i, next);
            const auto [whiteElo, blackElo] = m_impl->averages(i, next);
            explored.moves.push_back({detail::toCore(*move), chess::uci::moveToSan(board, *move),
                                      next - i, results[0], results[1], results[2], whiteElo,
                                      blackElo});
        }
        i = next;
    }
    std::stable_sort(explored.moves.begin(), explored.moves.end(),
                     [](const MoveStats& a, const MoveStats& b) { return a.games > b.games; });
    return explored;
}

std::vector<GameInfo> Database::gamesAt(std::span<const core::Move> line, std::size_t limit,
                                        const ExploreFilter& filter) const {
    chess::Board board;
    const auto prefix = Impl::prefixOf(line, board);
    if (!prefix || limit == 0) {
        return {};
    }
    const auto [lo, hi] = m_impl->range(*prefix);
    // The best rated: keep the `limit` highest while going through the range once.
    const auto* order = m_impl->order.as<std::uint32_t>();
    const auto* elo = m_impl->elo.as<std::uint16_t>();
    using Entry = std::pair<std::uint16_t, std::uint32_t>; // rating, game
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> best;
    const int human = highestHumanRating(m_impl->info);
    const bool filtered = filter.active();
    const Impl::Matcher match = filtered ? m_impl->matcher(filter) : Impl::Matcher{};
    for (std::size_t i = lo; i < hi; ++i) {
        const Entry entry{elo[order[i]], order[i]};
        if (std::cmp_greater(entry.first, human)) {
            continue; // engine games would fill the list
        }
        if (filtered && !match(i)) {
            continue;
        }
        if (best.size() < limit) {
            best.push(entry);
        } else if (entry.first > best.top().first) {
            best.pop();
            best.push(entry);
        }
    }
    std::vector<GameId> ids;
    while (!best.empty()) {
        ids.push_back(static_cast<GameId>(best.top().second) + 1);
        best.pop();
    }
    std::reverse(ids.begin(), ids.end());
    return m_impl->byIds(ids);
}

} // namespace kiebitz::gamedb
