#pragma once

#include "kiebitz/core/game.hpp"
#include "kiebitz/core/result.hpp"
#include "kiebitz/gamedb/scan.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// A game database: millions of games in a folder, searchable, with an opening explorer.
///
/// The folder holds:
///   games.sqlite  the headers, with indexes for searching players, events and openings;
///   moves.bin     every game's main line, one byte per move;
///   offsets.bin   where each game's moves start in moves.bin;
///   order.bin     the games from the usual start position, sorted by their moves;
///   sums.bin      running totals of their results in that order;
///   elo.bin       each game's average rating;
///   ratings.bin   White's and Black's rating of each game, in the order of order.bin;
///   attrs.bin     each game's year, speed and players, in the same order (for filters).
/// Games with the same opening sit next to each other in order.bin, so a line's games are one
/// range found by binary search, and its statistics are two lookups in sums.bin: the explorer
/// answers in well under a millisecond for any number of games. (A position reached by another
/// move order is counted under that move order.)
namespace kiebitz::gamedb {

using GameId = std::int64_t; ///< 1, 2, ... in the order the games were imported

struct GameInfo {
    GameId id = 0;
    std::string white, black, event, site, date, round, eco;
    int whiteElo = 0;
    int blackElo = 0;
    GameResult result = GameResult::Unknown;
    int plies = 0;
};

/// A database's description (also kept in it).
struct DatabaseInfo {
    std::string name;
    std::string source; ///< where it came from: a file path or a download's name
    std::uint64_t games = 0;
    std::uint64_t players = 0;
    /// Most of its games were played online (lichess.org, chess.com): online blitz and bullet
    /// ratings go far higher than over-the-board ones, which matters for telling engines apart.
    bool online = false;
};

/// The highest rating a human has in a database; games rated above it are engines'. Over the
/// board no one has been above 2882; online, blitz and bullet ratings reach about 3300.
[[nodiscard]] constexpr int highestHumanRating(const DatabaseInfo& info) {
    return info.online ? 3300 : 2900;
}

/// What the explorer shows for a line: its games and results, and every next move played.
struct MoveStats {
    core::Move move;
    std::string san;
    std::uint64_t games = 0;
    std::uint64_t white = 0; ///< White won
    std::uint64_t draws = 0;
    std::uint64_t black = 0; ///< Black won
    int whiteElo = 0;        ///< White's average rating in these games (0: none known)
    int blackElo = 0;
};
struct Explored {
    std::uint64_t games = 0;
    std::uint64_t white = 0;
    std::uint64_t draws = 0;
    std::uint64_t black = 0;
    int whiteElo = 0;
    int blackElo = 0;
    std::vector<MoveStats> moves; ///< most played first
};

/// A player's results, from the player's side: wins, draws, losses with each colour.
struct Record {
    std::array<std::uint64_t, 3> asWhite{}; ///< won, drawn, lost
    std::array<std::uint64_t, 3> asBlack{};
    [[nodiscard]] std::uint64_t games() const {
        return asWhite[0] + asWhite[1] + asWhite[2] + asBlack[0] + asBlack[1] + asBlack[2];
    }
    Record& operator+=(const Record& other) {
        for (std::size_t k = 0; k < 3; ++k) {
            asWhite[k] += other.asWhite[k];
            asBlack[k] += other.asBlack[k];
        }
        return *this;
    }
};

/// Which games the explorer counts. Empty fields don't filter.
struct ExploreFilter {
    int minRating = 0; ///< the players' average rating at least this
    int maxRating = 0; ///< ... and at most this
    int fromYear = 0;
    int toYear = 0;
    std::uint8_t speeds = 0; ///< bits: 1 << Speed (Unknown included when any bit is set: never)
    /// Only the games of these players (any case)...
    std::vector<std::string> players;
    /// ... with this colour.
    enum class Side : std::uint8_t { Any, White, Black } side = Side::Any;
    /// Only games whose White (Black) player's name contains this (any case): "carlsen".
    std::string white;
    std::string black;

    [[nodiscard]] bool active() const {
        return minRating > 0 || maxRating > 0 || fromYear > 0 || toYear > 0 || speeds != 0 ||
               !players.empty() || !white.empty() || !black.empty();
    }
};

/// A player's results in total and by year (0: no date), for the profile's overview.
struct PlayerStats {
    Record total;
    std::map<int, Record> years;
};

/// Writes a new database: add() every game, then finish().
class DatabaseWriter {
public:
    /// Creates the folder's files (the folder must exist and be empty).
    [[nodiscard]] static core::Result<std::unique_ptr<DatabaseWriter>>
    create(const std::string& folder, DatabaseInfo info);
    ~DatabaseWriter();
    DatabaseWriter(const DatabaseWriter&) = delete;
    DatabaseWriter& operator=(const DatabaseWriter&) = delete;
    DatabaseWriter(DatabaseWriter&&) = delete;
    DatabaseWriter& operator=(DatabaseWriter&&) = delete;

    void add(const ScannedGame& game);
    [[nodiscard]] std::uint64_t count() const;

    /// Sorts the games and writes the indexes; `progress` gets 0..1. Empty on success, else
    /// the error.
    std::string finish(const std::function<void(double)>& progress = {});

private:
    struct Impl;
    explicit DatabaseWriter(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

/// Adds what a database made by an older Kiebitz lacks (ratings.bin, attrs.bin) from the
/// headers it has. Empty on success, else the error. A Database opened before keeps working
/// without them; one opened afterwards has them. (Speeds aren't in the headers kept: those
/// stay unknown.)
std::string_view upgrade(const std::string& folder);

/// Reads a finished database. Safe to use from several threads at once.
class Database {
public:
    [[nodiscard]] static core::Result<std::unique_ptr<Database>> open(const std::string& folder);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) = delete;
    Database& operator=(Database&&) = delete;

    [[nodiscard]] const DatabaseInfo& info() const;
    /// Whether it has ratings.bin and attrs.bin (older databases get them from upgrade()).
    [[nodiscard]] bool hasRatings() const;
    [[nodiscard]] bool hasAttributes() const;
    [[nodiscard]] std::uint64_t gameCount() const;

    /// Games by id, `count` of them from `first` (for scrolling through all of them).
    [[nodiscard]] std::vector<GameInfo> games(GameId first, std::size_t count) const;
    /// Games whose players, event, site or ECO contain `text` (any case), newest first.
    [[nodiscard]] std::vector<GameInfo> search(std::string_view text, std::size_t limit) const;
    /// A whole game: headers and moves.
    [[nodiscard]] std::optional<core::Game> game(GameId id) const;

    /// The results of the player(s) called any of `names` (any case), in finished games.
    [[nodiscard]] Record record(std::span<const std::string> names) const;
    /// The same, by year, and only at these speeds (bits: 1 << Speed; 0: all). Counts the games
    /// from the usual start position (all a player's games, in practice).
    [[nodiscard]] PlayerStats playerStats(std::span<const std::string> names,
                                          std::uint8_t speeds = 0) const;

    /// The explorer for a line of moves from the usual start position.
    [[nodiscard]] Explored explore(std::span<const core::Move> line,
                                   const ExploreFilter& filter = {}) const;
    /// The games of that line, best rated first (engine games left out: see
    /// highestHumanRating()).
    [[nodiscard]] std::vector<GameInfo> gamesAt(std::span<const core::Move> line, std::size_t limit,
                                                const ExploreFilter& filter = {}) const;

private:
    struct Impl;
    explicit Database(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace kiebitz::gamedb
