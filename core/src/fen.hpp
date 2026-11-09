#pragma once

#include "chessgui/core/result.hpp"

#include <string>
#include <string_view>

namespace chessgui::core::detail {

/// Checks a FEN strictly and returns it in normalized form (single spaces, all six fields,
/// castling rights limited to what the pieces allow). Rules that need move generation, like
/// "the side not to move is in check", are checked by Position::fromFen().
[[nodiscard]] Result<std::string> validateFen(std::string_view fen);

} // namespace chessgui::core::detail
