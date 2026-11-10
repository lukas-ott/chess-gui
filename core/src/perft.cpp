#include "kiebitz/core/perft.hpp"

#include <chess.hpp>

namespace kiebitz::core {
namespace {

std::uint64_t perftRecursive(chess::Board& board, int depth) {
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);

    if (depth == 1) {
        return static_cast<std::uint64_t>(moves.size());
    }

    std::uint64_t nodes = 0;
    for (const auto& move : moves) {
        board.makeMove(move);
        nodes += perftRecursive(board, depth - 1);
        board.unmakeMove(move);
    }
    return nodes;
}

} // namespace

std::uint64_t perft(std::string_view fen, int depth) {
    if (depth <= 0) {
        return 1;
    }
    chess::Board board(fen);
    return perftRecursive(board, depth);
}

} // namespace kiebitz::core
