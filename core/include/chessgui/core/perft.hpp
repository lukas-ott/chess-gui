#pragma once

#include <cstdint>
#include <string_view>

namespace chessgui::core {

inline constexpr std::string_view kStartFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

/// Counts the leaf nodes of the legal move tree to the given depth.
/// Used to verify move generation against known reference values.
[[nodiscard]] std::uint64_t perft(std::string_view fen, int depth);

} // namespace chessgui::core
