#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace chessgui::core {

enum class Color : std::uint8_t { White, Black };

[[nodiscard]] constexpr Color opposite(Color color) noexcept {
    return color == Color::White ? Color::Black : Color::White;
}

enum class PieceType : std::uint8_t { None, Pawn, Knight, Bishop, Rook, Queen, King };

struct Piece {
    PieceType type = PieceType::None;
    Color color = Color::White;

    [[nodiscard]] constexpr bool isNone() const noexcept { return type == PieceType::None; }

    /// FEN letter: uppercase for White ("N"), lowercase for Black ("n"), '\0' for no piece.
    [[nodiscard]] char fenChar() const noexcept;
    [[nodiscard]] static std::optional<Piece> fromFenChar(char c) noexcept;

    friend constexpr bool operator==(const Piece& a, const Piece& b) noexcept {
        return a.type == b.type && (a.type == PieceType::None || a.color == b.color);
    }
};

/// A square on the board, a1 = 0, b1 = 1, ..., h8 = 63 (rank * 8 + file).
class Square {
public:
    constexpr Square() noexcept = default;

    [[nodiscard]] static constexpr Square fromIndex(int index) noexcept {
        return Square(static_cast<std::uint8_t>(index));
    }
    /// file 0..7 = a..h, rank 0..7 = 1..8
    [[nodiscard]] static constexpr Square at(int file, int rank) noexcept {
        return fromIndex((rank * 8) + file);
    }
    /// Parses "e4". Returns nothing for anything else.
    [[nodiscard]] static std::optional<Square> parse(std::string_view name) noexcept;

    [[nodiscard]] constexpr int index() const noexcept { return m_index; }
    [[nodiscard]] constexpr int file() const noexcept { return m_index % 8; }
    [[nodiscard]] constexpr int rank() const noexcept { return m_index / 8; }
    [[nodiscard]] std::string name() const;

    friend constexpr auto operator<=>(const Square&, const Square&) noexcept = default;

private:
    constexpr explicit Square(std::uint8_t index) noexcept : m_index(index) {}

    std::uint8_t m_index = 0;
};

/// A move as the user sees it. Castling is the king's move (e1g1), as in UCI.
struct Move {
    Square from;
    Square to;
    PieceType promotion = PieceType::None;

    /// UCI notation: "e2e4", "e7e8q".
    [[nodiscard]] std::string uci() const;
    /// Parses UCI notation. Says nothing about legality; see Position::parseUci().
    [[nodiscard]] static std::optional<Move> fromUci(std::string_view uci) noexcept;

    friend constexpr bool operator==(const Move&, const Move&) noexcept = default;
};

} // namespace chessgui::core
