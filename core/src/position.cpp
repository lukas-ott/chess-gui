#include "kiebitz/core/position.hpp"

#include "fen.hpp"

#include <chess.hpp>

#include <cassert>
#include <cstdlib>
#include <utility>

namespace kiebitz::core {

// ---- Conversions between our types and chess-library's ----------------------------------------
namespace {

chess::Square toLib(Square square) {
    return {square.index()};
}

Square fromLib(chess::Square square) {
    return Square::fromIndex(square.index());
}

PieceType fromLib(chess::PieceType type) {
    // chess-library: PAWN = 0 ... KING = 5, NONE = 6. Ours: None = 0, Pawn = 1 ... King = 6.
    const auto value = static_cast<int>(type.internal());
    return value >= 6 ? PieceType::None : static_cast<PieceType>(value + 1);
}

/// chess-library writes castling as "king takes own rook" (e1h1). Ours is the king's move (e1g1).
Move fromLib(const chess::Move& move) {
    Move result{fromLib(move.from()), fromLib(move.to())};
    if (move.typeOf() == chess::Move::CASTLING) {
        const int file = move.to().index() > move.from().index() ? 6 : 2;
        result.to = Square::at(file, result.from.rank());
    } else if (move.typeOf() == chess::Move::PROMOTION) {
        result.promotion = fromLib(move.promotionType());
    }
    return result;
}

} // namespace

struct Position::Impl {
    chess::Board board;

    /// The library move for one of our moves, or nothing if it isn't legal here.
    [[nodiscard]] std::optional<chess::Move> find(Move move) const {
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, board);
        for (const auto& candidate : moves) {
            if (fromLib(candidate) == move) {
                return candidate;
            }
        }
        return std::nullopt;
    }
};

// ---- Construction ------------------------------------------------------------------------------

Position::Position() : m_impl(std::make_unique<Impl>()) {
    m_impl->board.setFen(kStartFen);
}

Position::Position(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

Position::Position(const Position& other) : m_impl(std::make_unique<Impl>(*other.m_impl)) {}
Position::Position(Position&& other) noexcept = default;
Position& Position::operator=(const Position& other) {
    if (this != &other) {
        m_impl = std::make_unique<Impl>(*other.m_impl);
    }
    return *this;
}
Position& Position::operator=(Position&& other) noexcept = default;
Position::~Position() = default;

Result<Position> Position::fromFen(std::string_view fen) {
    auto normalized = detail::validateFen(fen);
    if (!normalized) {
        return normalized.error();
    }
    auto impl = std::make_unique<Impl>();
    if (!impl->board.setFen(*normalized)) {
        return Error{"The FEN could not be read."};
    }
    Position position(std::move(impl));
    // Checked here rather than in validateFen because it needs attack generation.
    const Color mover = position.sideToMove();
    const auto& board = position.m_impl->board;
    const auto otherSide = mover == Color::White ? chess::Color::BLACK : chess::Color::WHITE;
    if (board.isAttacked(board.kingSq(otherSide), ~otherSide)) {
        return Error{"The side that is not to move is in check."};
    }
    return position;
}

// ---- Queries -----------------------------------------------------------------------------------

std::string Position::fen() const {
    return m_impl->board.getFen();
}

Color Position::sideToMove() const noexcept {
    return m_impl->board.sideToMove() == chess::Color::WHITE ? Color::White : Color::Black;
}

Piece Position::pieceAt(Square square) const noexcept {
    const auto piece = m_impl->board.at(toLib(square));
    if (piece == chess::Piece::NONE) {
        return {};
    }
    return {fromLib(piece.type()),
            piece.color() == chess::Color::WHITE ? Color::White : Color::Black};
}

int Position::pieceCount() const noexcept {
    return m_impl->board.occ().count();
}

CastlingRights Position::castlingRights() const noexcept {
    using Side = chess::Board::CastlingRights::Side;
    const auto rights = m_impl->board.castlingRights();
    return {rights.has(chess::Color::WHITE, Side::KING_SIDE),
            rights.has(chess::Color::WHITE, Side::QUEEN_SIDE),
            rights.has(chess::Color::BLACK, Side::KING_SIDE),
            rights.has(chess::Color::BLACK, Side::QUEEN_SIDE)};
}

std::optional<Square> Position::enPassantSquare() const noexcept {
    const auto square = m_impl->board.enpassantSq();
    if (square == chess::Square::NO_SQ) {
        return std::nullopt;
    }
    return fromLib(square);
}

int Position::halfmoveClock() const noexcept {
    return static_cast<int>(m_impl->board.halfMoveClock());
}

int Position::fullmoveNumber() const noexcept {
    return static_cast<int>(m_impl->board.fullMoveNumber());
}

std::uint64_t Position::hash() const noexcept {
    return m_impl->board.hash();
}

// ---- Moves -------------------------------------------------------------------------------------

std::vector<Move> Position::legalMoves() const {
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, m_impl->board);
    std::vector<Move> result;
    result.reserve(moves.size());
    for (const auto& move : moves) {
        result.push_back(fromLib(move));
    }
    return result;
}

bool Position::isLegal(Move move) const {
    return m_impl->find(move).has_value();
}

void Position::play(Move move) {
    const auto libMove = m_impl->find(move);
    assert(libMove && "Position::play() needs a legal move");
    if (libMove) {
        m_impl->board.makeMove(*libMove);
    }
}

bool Position::inCheck() const noexcept {
    return m_impl->board.inCheck();
}

bool Position::isCheckmate() const {
    return inCheck() && legalMoves().empty();
}

bool Position::isStalemate() const {
    return !inCheck() && legalMoves().empty();
}

bool Position::isInsufficientMaterial() const noexcept {
    return m_impl->board.isInsufficientMaterial();
}

std::string Position::san(Move move) const {
    const auto libMove = m_impl->find(move);
    assert(libMove && "Position::san() needs a legal move");
    if (!libMove) {
        return move.uci();
    }
    return chess::uci::moveToSan(m_impl->board, *libMove);
}

Result<Move> Position::parseUci(std::string_view uci) const {
    const auto move = Move::fromUci(uci);
    if (!move) {
        return Error{"\"" + std::string(uci) + "\" is not a UCI move."};
    }
    if (isLegal(*move)) {
        return *move;
    }
    // Castling written as the king taking its own rook ("e1h1"), as Chess960 notation and the
    // Lichess cloud do: the same move as the king's two-square step.
    const Piece king = pieceAt(move->from);
    const Piece rook = pieceAt(move->to);
    if (king.type == PieceType::King && rook.type == PieceType::Rook && king.color == rook.color &&
        move->from.rank() == move->to.rank()) {
        const int file = move->to.file() > move->from.file() ? 6 : 2;
        const Move castle{move->from, Square::at(file, move->from.rank())};
        if (isLegal(castle)) {
            return castle;
        }
    }
    return Error{"\"" + std::string(uci) + "\" is not a legal move here."};
}

// ---- SAN parsing -------------------------------------------------------------------------------
namespace {

struct SanParts {
    PieceType piece = PieceType::Pawn;
    std::optional<int> fromFile;
    std::optional<int> fromRank;
    Square to;
    PieceType promotion = PieceType::None;
    int castling = 0; // 0 = none, 1 = king side, 2 = queen side
};

bool isFile(char c) {
    return c >= 'a' && c <= 'h';
}
bool isRank(char c) {
    return c >= '1' && c <= '8';
}

std::optional<PieceType> pieceFromLetter(char c) {
    switch (c) {
    case 'N':
        return PieceType::Knight;
    case 'B':
        return PieceType::Bishop;
    case 'R':
        return PieceType::Rook;
    case 'Q':
        return PieceType::Queen;
    case 'K':
        return PieceType::King;
    default:
        return std::nullopt;
    }
}

/// Splits SAN into its parts without looking at the board.
std::optional<SanParts> splitSan(std::string_view san) {
    // Drop check, mate and annotation marks: "Nf3+", "Qxf7#", "e4!?", "exd6 e.p."
    if (san.ends_with("e.p.")) {
        san.remove_suffix(4);
    }
    while (!san.empty() && (san.back() == '+' || san.back() == '#' || san.back() == '!' ||
                            san.back() == '?' || san.back() == ' ')) {
        san.remove_suffix(1);
    }

    SanParts parts;
    if (san == "O-O" || san == "0-0") {
        parts.castling = 1;
        return parts;
    }
    if (san == "O-O-O" || san == "0-0-0") {
        parts.castling = 2;
        return parts;
    }

    // Promotion at the end: "e8=Q", "e8Q", "e8=q". A letter after the rank digit can only be a
    // promotion piece, so lowercase is safe to accept.
    if (san.size() >= 3) {
        const char last = san.back();
        const auto upper = static_cast<char>(last >= 'a' && last <= 'z' ? last - 'a' + 'A' : last);
        const auto promo = pieceFromLetter(upper);
        const bool hasEquals = san[san.size() - 2] == '=';
        const std::size_t letters = hasEquals ? 2 : 1;
        if (promo && *promo != PieceType::King && san.size() > letters + 1 &&
            isRank(san[san.size() - letters - 1])) {
            parts.promotion = *promo;
            san.remove_suffix(letters);
        }
    }

    if (san.size() < 2) {
        return std::nullopt;
    }
    const auto to = Square::parse(san.substr(san.size() - 2));
    if (!to) {
        return std::nullopt;
    }
    parts.to = *to;
    san.remove_suffix(2);

    if (!san.empty()) {
        if (const auto piece = pieceFromLetter(san.front())) {
            parts.piece = *piece;
            san.remove_prefix(1);
        }
    }
    // What's left is disambiguation and/or 'x' / ':' for captures, in any reasonable order.
    for (const char c : san) {
        if (c == 'x' || c == ':' || c == '-') {
            continue;
        }
        if (isFile(c) && !parts.fromFile) {
            parts.fromFile = c - 'a';
        } else if (isRank(c) && !parts.fromRank) {
            parts.fromRank = c - '1';
        } else {
            return std::nullopt;
        }
    }
    if (parts.piece != PieceType::Pawn && parts.promotion != PieceType::None) {
        return std::nullopt;
    }
    return parts;
}

} // namespace

Result<Move> Position::parseSan(std::string_view san) const {
    const auto quoted = "\"" + std::string(san) + "\"";
    const auto parts = splitSan(san);
    if (!parts) {
        return Error{quoted + " is not a valid move."};
    }

    std::optional<Move> found;
    int matches = 0;
    for (const Move& move : legalMoves()) {
        const Piece piece = pieceAt(move.from);
        if (parts->castling != 0) {
            const bool isCastle =
                piece.type == PieceType::King && std::abs(move.to.file() - move.from.file()) == 2;
            if (!isCastle || (move.to.file() == 6) != (parts->castling == 1)) {
                continue;
            }
        } else {
            if (piece.type != parts->piece || move.to != parts->to) {
                continue;
            }
            if ((parts->fromFile && move.from.file() != *parts->fromFile) ||
                (parts->fromRank && move.from.rank() != *parts->fromRank)) {
                continue;
            }
            if (move.promotion != parts->promotion) {
                if (parts->promotion == PieceType::None) {
                    return Error{quoted + " needs a promotion piece, e.g. " + parts->to.name() +
                                 "=Q."};
                }
                continue;
            }
        }
        found = move;
        ++matches;
    }

    if (matches == 0 || !found) {
        return Error{quoted + " is not a legal move here."};
    }
    if (matches > 1) {
        return Error{quoted + " is ambiguous: more than one piece can make that move."};
    }
    return *found;
}

} // namespace kiebitz::core
