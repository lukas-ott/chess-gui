#pragma once

#include "kiebitz/core/position.hpp" // kStartFen

#include <cstdint>
#include <string_view>

namespace kiebitz::core {

/// Counts the leaf nodes of the legal move tree to the given depth.
/// Used to verify move generation against known reference values.
[[nodiscard]] std::uint64_t perft(std::string_view fen, int depth);

} // namespace kiebitz::core
