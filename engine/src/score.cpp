#include "kiebitz/engine/score.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace kiebitz::engine {

double winningChances(const Score& score) noexcept {
    if (score.kind == Score::Kind::Mate || score.kind == Score::Kind::Over) {
        if (score.value == 0) {
            return 0.0; // a drawn game (a "mate in 0" never comes from an engine with a line)
        }
        return score.value > 0 ? 1.0 : -1.0;
    }
    // Lichess's curve (fitted to its games), clamped so a huge advantage still leaves a sliver.
    constexpr double kSlope = -0.00368208;
    const double cp = std::clamp(static_cast<double>(score.value), -1000.0, 1000.0);
    return (2.0 / (1.0 + std::exp(kSlope * cp))) - 1.0;
}

std::string formatScore(const Score& score) {
    // (Built by hand: Apple's standard library has no std::format.)
    if (score.kind == Score::Kind::Over) {
        if (score.value == 0) {
            return "\u00BD-\u00BD";
        }
        return score.value > 0 ? "1-0" : "0-1";
    }
    if (score.kind == Score::Kind::Mate) {
        return (score.value >= 0 ? "#" : "-#") + std::to_string(std::abs(score.value));
    }
    if (score.value == 0) {
        return "0.00";
    }
    const int cp = std::abs(score.value);
    const int hundredths = cp % 100;
    return (score.value > 0 ? "+" : "-") + std::to_string(cp / 100) + "." +
           (hundredths < 10 ? "0" : "") + std::to_string(hundredths);
}

} // namespace kiebitz::engine
