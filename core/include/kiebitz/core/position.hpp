#pragma once

#include "kiebitz/core/result.hpp"
#include "kiebitz/core/types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiebitz::core {

inline constexpr std::string_view kStartFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

struct CastlingRights {
    bool whiteKingSide = false;
    bool whiteQueenSide = false;
    bool blackKingSide = false;
    bool blackQueenSide = false;

    friend constexpr bool operator==(const CastlingRights&, const CastlingRights&) = default;
};

/// A legal chess position: pieces, side to move, castling rights, en passant, move counters.
///
/// It's a value type (copying gives an independent position). Move generation is done by
/// chess-library, which stays hidden behind this class.
class Position {
public:
    /// The standard starting position.
    Position();
    /// Parses and validates a FEN. Accepts FENs without the two move counters. Castling rights
    /// that the pieces don't allow (king or rook not on its home square) are dropped, as is an
    /// en passant square no pawn can capture on.
    [[nodiscard]] static Result<Position> fromFen(std::string_view fen);

    Position(const Position& other);
    Position(Position&& other) noexcept;
    Position& operator=(const Position& other);
    Position& operator=(Position&& other) noexcept;
    ~Position();

    [[nodiscard]] std::string fen() const;
    [[nodiscard]] Color sideToMove() const noexcept;
    [[nodiscard]] Piece pieceAt(Square square) const noexcept;
    [[nodiscard]] CastlingRights castlingRights() const noexcept;
    [[nodiscard]] std::optional<Square> enPassantSquare() const noexcept;
    [[nodiscard]] int halfmoveClock() const noexcept;
    [[nodiscard]] int fullmoveNumber() const noexcept;
    /// Zobrist hash: equal positions (pieces, side, castling, en passant) have equal hashes.
    [[nodiscard]] std::uint64_t hash() const noexcept;

    [[nodiscard]] std::vector<Move> legalMoves() const;
    [[nodiscard]] bool isLegal(Move move) const;
    /// Plays a move. It must be legal (check with isLegal() when unsure).
    void play(Move move);

    [[nodiscard]] bool inCheck() const noexcept;
    [[nodiscard]] bool isCheckmate() const;
    [[nodiscard]] bool isStalemate() const;
    [[nodiscard]] bool isInsufficientMaterial() const noexcept;

    /// Standard Algebraic Notation for a legal move, with "+" or "#" ("Nbd7", "exd8=Q+", "O-O").
    [[nodiscard]] std::string san(Move move) const;
    /// Finds the legal move a SAN string means. Lenient about what PGN files contain in practice:
    /// a missing or extra "x", "0-0" for "O-O", "e8Q" for "e8=Q", trailing "+", "#", "!", "?".
    [[nodiscard]] Result<Move> parseSan(std::string_view san) const;
    /// Finds the legal move for UCI notation ("e2e4", "e7e8q").
    [[nodiscard]] Result<Move> parseUci(std::string_view uci) const;

private:
    struct Impl;
    explicit Position(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace kiebitz::core
