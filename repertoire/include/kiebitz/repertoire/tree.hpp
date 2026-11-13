#pragma once

#include "kiebitz/core/game.hpp"
#include "kiebitz/core/position.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

/// Repertoires: the games of one or more PGN files merged into one tree of positions.
///
/// A repertoire is kept as ordinary PGN (any number of files and chapters). Here the moves are
/// merged by position (Zobrist hash), so a line reached by a transposition is one line, and
/// "what do I play here?" has one answer whichever file it came from.
namespace kiebitz::repertoire {

/// Where a position occurs in the games the tree was built from.
struct Source {
    int file = 0; ///< index of the file, as the caller numbered them
    int game = 0; ///< index of the game in that file
    core::NodeId node = core::kNoNode;

    friend bool operator==(const Source&, const Source&) = default;
};

/// One move from a position, as the repertoire has it.
struct Continuation {
    core::Move move;
    std::string san;
    std::uint64_t to = 0; ///< the position after it
    std::string comment;  ///< the first comment found on this move (raw, with any [%...])
    /// It is the main continuation in at least one game. For our side that makes it the
    /// repertoire's move; a move only ever in side variations is an alternative (books give
    /// them as "8. f3 is also possible").
    bool main = false;
};

/// A position in the repertoire.
struct Entry {
    std::vector<Continuation> moves; ///< in the order first seen (main lines first)
    std::vector<Source> sources;     ///< everywhere it occurs
    core::Color toMove = core::Color::White;
    int ply = 0; ///< the shortest way to reach it, in plies from the start
};

/// How far a line follows the repertoire.
struct Book {
    int plies = 0;     ///< moves of the line that are in the repertoire
    bool left = false; ///< the line goes on after the repertoire ends or differs
    bool byUs = false; ///< ... and it was our move there (we deviated, or have no move)
    std::vector<Continuation> expected; ///< what the repertoire has where the line left it
};

/// Counts for the overview.
struct Stats {
    std::size_t positions = 0;
    std::size_t ourMoves = 0;  ///< positions where it is our move
    std::size_t conflicts = 0; ///< our move, but more than one main answer (files disagree)
    std::size_t openEnds = 0;  ///< our move, but no answer: the line stops after their move
    int depth = 0;             ///< the longest line, in full moves
};

/// Several games as one, for practising a whole repertoire: their moves merged into one tree
/// (the same move from the same node once), the first game's lines first, so where games
/// disagree the first one's main move stays the main one. A move keeps the first comment,
/// annotations and arrows found for it. Games that start from another position are left out.
[[nodiscard]] core::Game mergeGames(std::span<const core::Game> games);

class Tree {
public:
    /// An empty repertoire for `side` (the side whose moves are "ours").
    explicit Tree(core::Color side = core::Color::White);

    [[nodiscard]] core::Color side() const noexcept { return m_side; }
    [[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

    /// Adds every move of a game (main line and variations). `file` and `game` say where it came
    /// from, for edits later.
    void add(const core::Game& game, int file, int index);

    /// The position, or null if the repertoire doesn't have it.
    [[nodiscard]] const Entry* find(std::uint64_t position) const;
    [[nodiscard]] bool isOurMove(const Entry& entry) const noexcept {
        return entry.toMove == m_side;
    }

    /// Follows a line (moves from `start`) through the repertoire.
    [[nodiscard]] Book follow(const core::Position& start, std::span<const core::Move> line) const;

    /// How many different positions lie below this one (itself not counted).
    [[nodiscard]] std::size_t reach(std::uint64_t position) const;

    [[nodiscard]] Stats stats() const;
    /// Positions where it is our move and the repertoire has more than one main answer: games
    /// that disagree (alternatives in side variations don't count).
    [[nodiscard]] std::vector<std::uint64_t> conflicts() const;
    /// Positions where it is our move and the repertoire has no answer, shallowest first.
    [[nodiscard]] std::vector<std::uint64_t> openEnds() const;

private:
    core::Color m_side;
    std::unordered_map<std::uint64_t, Entry> m_entries;
};

} // namespace kiebitz::repertoire
