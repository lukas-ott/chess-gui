#include "codec.hpp"

#include <algorithm>

namespace kiebitz::gamedb::detail {
namespace {

/// The destination as players see it: chess-library writes castling as "king takes rook".
int targetOf(chess::Move move) {
    if (move.typeOf() == chess::Move::CASTLING) {
        const int rank = move.from().index() / 8;
        const int file = move.to().index() > move.from().index() ? 6 : 2;
        return (rank * 8) + file;
    }
    return move.to().index();
}

/// The fixed order: from square, then to square, then the promotion piece.
std::uint16_t keyOf(chess::Move move) {
    const unsigned promotion = move.typeOf() == chess::Move::PROMOTION
                                   ? static_cast<unsigned>(move.promotionType().internal()) + 1U
                                   : 0U;
    return static_cast<std::uint16_t>((static_cast<unsigned>(move.from().index()) << 9U) |
                                      (static_cast<unsigned>(targetOf(move)) << 3U) | promotion);
}

} // namespace

void orderedMoves(const chess::Board& board, chess::Movelist& moves) {
    chess::movegen::legalmoves(moves, board);
    std::sort(moves.begin(), moves.end(),
              [](chess::Move a, chess::Move b) { return keyOf(a) < keyOf(b); });
}

std::optional<std::uint8_t> encode(const chess::Board& board, chess::Move move) {
    // Its index in the sorted list is how many legal moves sort before it: no sorting needed.
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    const std::uint16_t key = keyOf(move);
    int before = 0;
    bool legal = false;
    for (const chess::Move candidate : moves) {
        if (candidate == move) {
            legal = true;
        } else if (keyOf(candidate) < key) {
            ++before;
        }
    }
    return legal ? std::optional(static_cast<std::uint8_t>(before)) : std::nullopt;
}

std::optional<chess::Move> decode(const chess::Board& board, std::uint8_t byte) {
    chess::Movelist moves;
    orderedMoves(board, moves);
    if (byte >= moves.size()) {
        return std::nullopt;
    }
    return moves[byte];
}

core::Move toCore(chess::Move move) {
    core::Move result{core::Square::fromIndex(move.from().index()),
                      core::Square::fromIndex(targetOf(move))};
    if (move.typeOf() == chess::Move::PROMOTION) {
        // chess-library: KNIGHT = 1 ... QUEEN = 4; ours: Knight = 2 ... Queen = 5.
        result.promotion =
            static_cast<core::PieceType>(static_cast<int>(move.promotionType().internal()) + 1);
    }
    return result;
}

std::optional<chess::Move> fromCore(const chess::Board& board, core::Move move) {
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    for (const chess::Move candidate : moves) {
        if (toCore(candidate) == move) {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace kiebitz::gamedb::detail
