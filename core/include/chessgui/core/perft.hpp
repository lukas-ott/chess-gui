#pragma once

#include "chessgui/core/position.hpp" // kStartFen

#include <cstdint>
#include <string_view>

namespace chessgui::core {

/// Counts the leaf nodes of the legal move tree to the given depth.
/// Used to verify move generation against known reference values.
[[nodiscard]] std::uint64_t perft(std::string_view fen, int depth);

} // namespace chessgui::core
