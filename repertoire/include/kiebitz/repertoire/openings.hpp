#pragma once

#include "kiebitz/core/position.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kiebitz::repertoire {

/// A named opening position: "B90", "Sicilian Defense: Najdorf Variation".
struct Opening {
    std::string eco;
    std::string name;
};

/// Opening names by position, from the Lichess opening list (lichess-org/chess-openings, CC0):
/// lines of "eco<TAB>name<TAB>1. e4 c5 2. Nf3 ...". Looked up by position, so a transposed move
/// order finds its name too.
class OpeningNames {
public:
    OpeningNames() = default;
    /// Reads the list; lines that don't parse are skipped.
    [[nodiscard]] static OpeningNames fromTsv(std::string_view tsv);

    [[nodiscard]] std::size_t size() const noexcept { return m_names.size(); }
    /// The opening of exactly this position, or null.
    [[nodiscard]] const Opening* find(std::uint64_t position) const;
    /// The last named position along a line (moves from `start`), or null: the name a game
    /// "is" at its current move, even after it has left the named lines.
    [[nodiscard]] const Opening* along(const core::Position& start,
                                       std::span<const core::Move> line) const;

private:
    std::unordered_map<std::uint64_t, Opening> m_names;
};

} // namespace kiebitz::repertoire
