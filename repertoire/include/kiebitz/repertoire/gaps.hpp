#pragma once

#include "kiebitz/core/position.hpp"
#include "kiebitz/repertoire/tree.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

/// The gap check: opponent moves a repertoire doesn't answer yet, ranked by how likely you are to
/// meet them, judged by the games of a database or explorer.
namespace kiebitz::repertoire {

/// How often a move was played in a position, as a database or explorer counts it.
struct MoveCount {
    core::Move move;
    std::uint64_t games = 0;
};

/// The moves played after a line from the usual start (most played first or any order). May be
/// slow (a network explorer): it is only called from a background thread.
using Explore = std::function<std::vector<MoveCount>(std::span<const core::Move> line)>;

struct GapOptions {
    double minShare = 0.05;       ///< a move played less often than this, where it can be, is fine
    double minLikelihood = 0.005; ///< ... and so is one this unlikely to be reached from the start
    std::uint64_t minGames = 20;  ///< too few games in a position to judge it
    std::size_t maxPositions = 400; ///< how many positions to ask about at most
    std::size_t maxGaps = 30;       ///< how many gaps to report at most
};

struct Gap {
    std::vector<core::Move> line; ///< from the start to the position where the opponent moves
    core::Move move;              ///< the opponent's move the repertoire doesn't answer
    std::string san;
    std::uint64_t games = 0; ///< how often it was played there
    double share = 0;        ///< of the games in that position
    double likelihood = 0;   ///< of meeting it from the start, playing the repertoire
};

struct GapReport {
    std::vector<Gap> gaps; ///< most likely first
    std::size_t asked = 0; ///< positions asked about
    bool stopped = false;  ///< hit maxPositions (or was cancelled) before the end
};

/// Walks the repertoire from the start: our moves (the main ones) and the opponent moves it
/// answers, as long as they are likely enough to be met. Where the opponent moves, it asks
/// `explore` what is played and reports the moves the repertoire lacks. Positions the repertoire
/// doesn't have (a line that ends after our move) aren't gaps: lines end somewhere.
/// `cancelled` (optional) is checked between positions.
[[nodiscard]] GapReport findGaps(const Tree& tree, const Explore& explore,
                                 const GapOptions& options = {},
                                 const std::function<bool()>& cancelled = {});

} // namespace kiebitz::repertoire
