#include "fen.hpp"

#include "kiebitz/core/types.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

namespace kiebitz::core::detail {
namespace {

std::vector<std::string_view> splitFields(std::string_view text) {
    std::vector<std::string_view> fields;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
            ++pos;
        }
        const std::size_t start = pos;
        while (pos < text.size() && text[pos] != ' ' && text[pos] != '\t') {
            ++pos;
        }
        if (pos > start) {
            fields.push_back(text.substr(start, pos - start));
        }
    }
    return fields;
}

/// A non-negative whole number ("0", "42"). Nothing for signs, other characters or overflow.
std::optional<int> parseCount(std::string_view text) {
    constexpr int kMax = 1'000'000;
    if (text.empty()) {
        return std::nullopt;
    }
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9' || value > kMax) {
            return std::nullopt;
        }
        value = (value * 10) + (c - '0');
    }
    return value;
}

using Board = std::array<Piece, 64>;

Result<Board> parseBoard(std::string_view field) {
    Board board{};
    int rank = 7;
    int file = 0;
    for (const char c : field) {
        if (c == '/') {
            if (file != 8) {
                return Error{"Rank " + std::to_string(rank + 1) + " has " + std::to_string(file) +
                             " squares instead of 8."};
            }
            if (--rank < 0) {
                return Error{"The board has more than 8 ranks."};
            }
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else if (const auto piece = Piece::fromFenChar(c)) {
            if (file < 8) {
                board[static_cast<std::size_t>(Square::at(file, rank).index())] = *piece;
            }
            ++file;
        } else {
            return Error{std::string("'") + c + "' is not a piece letter or digit."};
        }
        if (file > 8) {
            return Error{"Rank " + std::to_string(rank + 1) + " has more than 8 squares."};
        }
    }
    if (rank != 0 || file != 8) {
        return Error{rank != 0 ? "The board has fewer than 8 ranks."
                               : "Rank 1 has " + std::to_string(file) + " squares instead of 8."};
    }
    return board;
}

Piece at(const Board& board, int file, int rank) {
    return board[static_cast<std::size_t>(Square::at(file, rank).index())];
}

} // namespace

Result<std::string> validateFen(std::string_view fen) {
    const auto fields = splitFields(fen);
    if (fields.size() < 4 || fields.size() > 6) {
        return Error{"A FEN has 4 to 6 fields separated by spaces; this one has " +
                     std::to_string(fields.size()) + "."};
    }

    // 1. Pieces
    auto board = parseBoard(fields[0]);
    if (!board) {
        return board.error();
    }
    std::array<int, 2> kings{};
    for (int i = 0; i < 64; ++i) {
        const Piece piece = (*board)[static_cast<std::size_t>(i)];
        if (piece.type == PieceType::King) {
            ++kings[static_cast<std::size_t>(piece.color)];
        }
        const int rank = Square::fromIndex(i).rank();
        if (piece.type == PieceType::Pawn && (rank == 0 || rank == 7)) {
            return Error{"There is a pawn on " + Square::fromIndex(i).name() +
                         "; pawns can't stand on the first or last rank."};
        }
    }
    if (kings[0] != 1 || kings[1] != 1) {
        return Error{std::string(kings[0] != 1 ? "White" : "Black") +
                     " must have exactly one king."};
    }

    // 2. Side to move
    if (fields[1] != "w" && fields[1] != "b") {
        return Error{R"(The side to move must be "w" or "b", not ")" + std::string(fields[1]) +
                     "\"."};
    }
    const bool whiteToMove = fields[1] == "w";

    // 3. Castling: keep only rights the pieces still allow.
    std::string castling;
    if (fields[2] != "-") {
        for (const char c : fields[2]) {
            const bool known = std::string_view("KQkq").find(c) != std::string_view::npos;
            const bool repeated = castling.find(c) != std::string::npos;
            if (!known || repeated) {
                return Error{R"(The castling field must be "-" or letters from KQkq, not ")" +
                             std::string(fields[2]) + "\"."};
            }
            castling += c;
        }
    }
    const Piece whiteKing{PieceType::King, Color::White};
    const Piece blackKing{PieceType::King, Color::Black};
    const Piece whiteRook{PieceType::Rook, Color::White};
    const Piece blackRook{PieceType::Rook, Color::Black};
    std::string allowed;
    for (const char c : castling) {
        const bool ok =
            (c == 'K' && at(*board, 4, 0) == whiteKing && at(*board, 7, 0) == whiteRook) ||
            (c == 'Q' && at(*board, 4, 0) == whiteKing && at(*board, 0, 0) == whiteRook) ||
            (c == 'k' && at(*board, 4, 7) == blackKing && at(*board, 7, 7) == blackRook) ||
            (c == 'q' && at(*board, 4, 7) == blackKing && at(*board, 0, 7) == blackRook);
        if (ok) {
            allowed += c;
        }
    }
    if (allowed.empty()) {
        allowed = "-";
    }

    // 4. En passant square: must be on the right rank; dropped if no pawn just double-pushed.
    std::string enPassant = "-";
    if (fields[3] != "-") {
        const auto square = Square::parse(fields[3]);
        if (!square) {
            return Error{"\"" + std::string(fields[3]) + "\" is not an en passant square."};
        }
        if (square->rank() != (whiteToMove ? 5 : 2)) {
            return Error{"The en passant square must be on rank " +
                         std::string(whiteToMove ? "6" : "3") + " when " +
                         (whiteToMove ? "White" : "Black") + " is to move."};
        }
        const int pawnRank = whiteToMove ? 4 : 3;
        const Piece pushedPawn{PieceType::Pawn, whiteToMove ? Color::Black : Color::White};
        const auto& pieces = *board;
        if (pieces[static_cast<std::size_t>(square->index())].isNone() &&
            pieces[static_cast<std::size_t>(Square::at(square->file(), pawnRank).index())] ==
                pushedPawn) {
            enPassant = std::string(fields[3]);
        }
    }

    // 5. Move counters (optional)
    int halfmove = 0;
    int fullmove = 1;
    if (fields.size() >= 5) {
        const auto value = parseCount(fields[4]);
        if (!value) {
            return Error{"The halfmove clock must be a number, not \"" + std::string(fields[4]) +
                         "\"."};
        }
        halfmove = *value;
    }
    if (fields.size() == 6) {
        const auto value = parseCount(fields[5]);
        if (!value) {
            return Error{"The move number must be a number, not \"" + std::string(fields[5]) +
                         "\"."};
        }
        fullmove = std::max(*value, 1); // some tools write 0
    }

    return std::string(fields[0]) + ' ' + std::string(fields[1]) + ' ' + allowed + ' ' + enPassant +
           ' ' + std::to_string(halfmove) + ' ' + std::to_string(fullmove);
}

} // namespace kiebitz::core::detail
