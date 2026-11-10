#pragma once

#include "kiebitz/core/types.hpp"

#include <cstdint>
#include <string>

namespace kiebitz::engine {

/// An engine's evaluation: centipawns or moves to mate; or, with the game already over on the
/// board (checkmate, stalemate), who won.
struct Score {
    enum class Kind : std::uint8_t {
        Centipawns,
        Mate,
        Over, ///< value +1: this side won, -1: lost, 0: drawn
    };
    /// Whether the engine only knows the score is at least / at most this (a search window).
    enum class Bound : std::uint8_t { Exact, Lower, Upper };

    Kind kind = Kind::Centipawns;
    /// Centipawns, or moves to mate (negative: getting mated). From the point of view the
    /// caller says: engines report it for the side to move; see forWhite().
    int value = 0;
    Bound bound = Bound::Exact;

    friend constexpr bool operator==(const Score&, const Score&) = default;

    /// The same score from White's point of view, given who was to move.
    [[nodiscard]] constexpr Score forWhite(core::Color sideToMove) const noexcept {
        if (sideToMove == core::Color::White) {
            return *this;
        }
        Score flipped = *this;
        flipped.value = -value;
        if (bound != Bound::Exact) {
            flipped.bound = bound == Bound::Lower ? Bound::Upper : Bound::Lower;
        }
        return flipped;
    }
};

/// How likely White is to win, from -1 (Black wins) to +1 (White wins), as Lichess draws its
/// eval bar: a logistic curve over centipawns, and ±1 for a mate. `score` is from White's view.
[[nodiscard]] double winningChances(const Score& score) noexcept;

/// "+0.34", "-1.20", "0.00", "#3" (White mates in 3), "-#2" (Black mates in 2); for a game
/// that is over, "1-0", "0-1" or "½-½".
/// `score` is from White's view.
[[nodiscard]] std::string formatScore(const Score& score);

} // namespace kiebitz::engine
