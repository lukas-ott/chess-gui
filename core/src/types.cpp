#include "chessgui/core/types.hpp"

namespace chessgui::core {
namespace {

constexpr std::string_view kPieceLetters = "PNBRQK";

} // namespace

char Piece::fenChar() const noexcept {
    if (type == PieceType::None) {
        return '\0';
    }
    const char upper = kPieceLetters[static_cast<std::size_t>(type) - 1];
    return color == Color::White ? upper : static_cast<char>(upper - 'A' + 'a');
}

std::optional<Piece> Piece::fromFenChar(char c) noexcept {
    const bool white = c >= 'A' && c <= 'Z';
    const char upper = white ? c : static_cast<char>(c - 'a' + 'A');
    const auto pos = kPieceLetters.find(upper);
    if (pos == std::string_view::npos || (!white && (c < 'a' || c > 'z'))) {
        return std::nullopt;
    }
    return Piece{static_cast<PieceType>(pos + 1), white ? Color::White : Color::Black};
}

std::optional<Square> Square::parse(std::string_view name) noexcept {
    if (name.size() != 2 || name[0] < 'a' || name[0] > 'h' || name[1] < '1' || name[1] > '8') {
        return std::nullopt;
    }
    return Square::at(name[0] - 'a', name[1] - '1');
}

std::string Square::name() const {
    return {static_cast<char>('a' + file()), static_cast<char>('1' + rank())};
}

std::string Move::uci() const {
    std::string text = from.name() + to.name();
    if (promotion != PieceType::None) {
        text += Piece{promotion, Color::Black}.fenChar();
    }
    return text;
}

std::optional<Move> Move::fromUci(std::string_view uci) {
    if (uci.size() != 4 && uci.size() != 5) {
        return std::nullopt;
    }
    const auto from = Square::parse(uci.substr(0, 2));
    const auto to = Square::parse(uci.substr(2, 2));
    if (!from || !to) {
        return std::nullopt;
    }
    Move move{*from, *to};
    if (uci.size() == 5) {
        const auto piece = Piece::fromFenChar(uci[4]);
        if (!piece || piece->type == PieceType::Pawn || piece->type == PieceType::King) {
            return std::nullopt;
        }
        move.promotion = piece->type;
    }
    return move;
}

} // namespace chessgui::core
