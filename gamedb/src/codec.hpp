#pragma once

#include "kiebitz/core/types.hpp"

#include <chess.hpp>

#include <cstdint>
#include <optional>

/// How moves are stored: one byte each, the move's index among the legal moves of its position
/// in a fixed order (by from square, to square, promotion). The order is our own, not
/// chess-library's generation order, so a newer chess-library can't change what a byte means.
namespace kiebitz::gamedb::detail {

/// The legal moves of a position in the stored order.
void orderedMoves(const chess::Board& board, chess::Movelist& moves);

/// The byte for a legal move, or nothing if it isn't legal here.
[[nodiscard]] std::optional<std::uint8_t> encode(const chess::Board& board, chess::Move move);
/// The move for a byte, or nothing if the byte doesn't fit the position.
[[nodiscard]] std::optional<chess::Move> decode(const chess::Board& board, std::uint8_t byte);

/// Between our move type and chess-library's.
[[nodiscard]] core::Move toCore(chess::Move move);
[[nodiscard]] std::optional<chess::Move> fromCore(const chess::Board& board, core::Move move);

} // namespace kiebitz::gamedb::detail
