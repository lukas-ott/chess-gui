#pragma once

#include "kiebitz/core/position.hpp"
#include "kiebitz/repertoire/tree.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

/// The theory check: where a set of games left your repertoire, and who left it.
namespace kiebitz::repertoire {

/// How a game left the repertoire.
enum class Departure : std::uint8_t {
    You,      ///< you played another move than the repertoire's
    Opponent, ///< your opponent played a move the repertoire doesn't answer
    End,      ///< the repertoire ends there: no more preparation
};

/// One of your games, from the usual start.
struct CheckedGame {
    std::vector<core::Move> line;          ///< its main line
    core::Color side = core::Color::White; ///< the colour you had
};

/// Where games left the book in the same way: the same position, the same move.
struct DepartureGroup {
    Departure kind = Departure::You;
    std::vector<core::Move> line; ///< from the start to the position where it happened
    core::Move played;            ///< the move that left the book (End: the next move played)
    std::string san;
    /// What the repertoire has there: your main moves, or every move of theirs it answers.
    std::vector<std::string> expected;
    core::Color side = core::Color::White; ///< your colour in these games
    std::vector<std::size_t> games;        ///< indexes into the games checked
};

struct TheoryReport {
    std::vector<DepartureGroup> groups; ///< most games first
    std::size_t checked = 0;            ///< games checked (with a repertoire for your colour)
    std::size_t stayed = 0;             ///< games that never left the book (they ended in it)
    double averageBookMoves = 0;        ///< how long games stayed in the book, in full moves
};

/// Follows each game through the repertoire for the colour you had in it (`white` or `black`;
/// an empty tree: games with that colour are skipped) and groups where they left it.
[[nodiscard]] TheoryReport checkTheory(const Tree& white, const Tree& black,
                                       std::span<const CheckedGame> games);

} // namespace kiebitz::repertoire
