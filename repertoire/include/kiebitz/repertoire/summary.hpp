#pragma once

#include "kiebitz/core/game.hpp"
#include "kiebitz/repertoire/openings.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace kiebitz::repertoire {

/// A repertoire file at a glance, as its Library page shows it. It counts what practice would
/// ask: positions where it is your move (your main move; side variations of yours are
/// alternatives), every reply of the opponent.
struct Summary {
    std::size_t positions = 0; ///< positions to practise (your move, with an answer)
    std::size_t lines = 0;     ///< lines to the end of the tree
    int deepestMove = 0;       ///< the full move the longest line reaches
    struct Named {
        std::string name; ///< "Benko Gambit Accepted"
        std::size_t positions = 0;
    };
    /// Positions by the opening they are in (the last named position on the way), most first.
    /// Positions before any named one aren't counted here.
    std::vector<Named> openings;
};

/// Sums up the games of a repertoire file for `side`. `names` may be null (no openings then).
[[nodiscard]] Summary summarize(std::span<const core::Game> games, core::Color side,
                                const OpeningNames* names);

} // namespace kiebitz::repertoire
