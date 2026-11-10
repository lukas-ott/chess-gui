#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kiebitz::pgn::detail {

enum class TokenType : std::uint8_t {
    Tag,            ///< [Name "Value"]: text = name, value = value (still escaped)
    Comment,        ///< {...} or ; to end of line: text = the comment
    Nag,            ///< $14: text = "14"
    MoveNumber,     ///< 12. or 12...
    Move,           ///< e4, Nxf3+!?, O-O, -- (text as written)
    VariationStart, ///< (
    VariationEnd,   ///< )
    Result,         ///< 1-0, 0-1, 1/2-1/2, *
    End,            ///< no more input
};

struct Token {
    TokenType type = TokenType::End;
    std::string_view text;
    std::string_view value;
    std::size_t offset = 0; ///< where the token starts in the input
    std::size_t end = 0;    ///< one past its last byte
};

/// Splits PGN text into tokens. Works on a view of the text: no copies, no allocation.
/// (Not noexcept: string_view::substr could throw in theory, though offsets stay in range.)
/// Lenient: unknown characters are skipped, an unclosed comment runs to the end.
class Tokenizer {
public:
    explicit Tokenizer(std::string_view text) noexcept : m_text(text) {
        // Skip a UTF-8 byte-order mark: some tools (ChessBase among them) start files with one.
        if (m_text.starts_with("\xEF\xBB\xBF")) {
            m_pos = 3;
        }
    }

    [[nodiscard]] Token next();
    [[nodiscard]] std::size_t position() const noexcept { return m_pos; }

private:
    [[nodiscard]] Token readTag(std::size_t start);
    [[nodiscard]] Token readWord(std::size_t start);

    std::string_view m_text;
    std::size_t m_pos = 0;
};

/// The 1-based line number of a byte offset.
[[nodiscard]] std::size_t lineAt(std::string_view text, std::size_t offset);

} // namespace kiebitz::pgn::detail
