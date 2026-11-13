#pragma once

#include "kiebitz/core/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiebitz::core {

/// Colours for arrows and square marks, as in Lichess and ChessBase PGN comments.
enum class MarkColor : std::uint8_t { Green, Red, Blue, Yellow };

struct Arrow {
    Square from;
    Square to;
    MarkColor color = MarkColor::Green;

    friend constexpr bool operator==(const Arrow&, const Arrow&) = default;
};

struct SquareMark {
    Square square;
    MarkColor color = MarkColor::Green;

    friend constexpr bool operator==(const SquareMark&, const SquareMark&) = default;
};

/// A PGN comment split into its readable text and the board drawings embedded in it:
/// "{[%csl Gd4][%cal Ge2e4,Rd1d8] Controls the centre}".
struct CommentParts {
    std::string text;
    std::vector<Arrow> arrows;
    std::vector<SquareMark> squareMarks;

    friend bool operator==(const CommentParts&, const CommentParts&) = default;
};

/// Pulls [%cal ...] and [%csl ...] out of a PGN comment. Other commands such as [%clk 0:05:00]
/// stay in the text so they survive a round-trip. Malformed entries are dropped.
[[nodiscard]] CommentParts parseComment(std::string_view comment);
/// The reverse of parseComment(): "[%csl Gd4] [%cal Ge2e4] text".
[[nodiscard]] std::string formatComment(const CommentParts& parts);

/// A comment as the move list shows it. Lichess, chess.com and other tools put commands into
/// comments: [%clk 1:24:00] (clock), [%emt 0:00:05] (time used), [%eval 0.31] (engine),
/// [%anno "name", name] (who wrote the text after it). The text keeps only what a person wrote;
/// the clock and author are kept apart; other commands are left out. Display only: the game
/// keeps the comment as it was, so nothing is lost when it's saved.
struct CommentDisplay {
    std::string text;   ///< what a person wrote, without commands
    std::string clock;  ///< time left after the move, shortened: "1:24:00", "47:00", "0:09"
    std::string author; ///< who wrote the text, or empty
    friend bool operator==(const CommentDisplay&, const CommentDisplay&) = default;
};
[[nodiscard]] CommentDisplay displayComment(std::string_view comment);

/// What a person wrote in a comment, for editing: the [%...] commands left out, the text's own
/// line breaks kept. "[%clk 0:03:00] Best by test" → "Best by test".
[[nodiscard]] std::string commentText(std::string_view comment);
/// The comment with its text replaced and its commands kept (first, in their order): what an
/// edited comment becomes, so editing the words never loses the clock or the engine's eval.
[[nodiscard]] std::string withCommentText(std::string_view comment, std::string_view text);

/// What drawing on the board does, as on Lichess: drawing an arrow (or marking a square) that is
/// already there in the same colour removes it; in another colour it changes the colour;
/// otherwise it is added.
void toggleArrow(std::vector<Arrow>& arrows, Arrow arrow);
void toggleSquareMark(std::vector<SquareMark>& marks, SquareMark mark);

/// Numeric Annotation Glyphs ($1, $2, ...): the symbol for display, e.g. 1 → "!", 14 → "⩲".
/// Empty for NAGs without a common symbol.
[[nodiscard]] std::string_view nagSymbol(std::uint8_t nag) noexcept;
/// The NAG for a symbol written directly after a move in PGN ("!?" → 5), if any.
[[nodiscard]] std::optional<std::uint8_t> nagFromSymbol(std::string_view symbol) noexcept;

/// Common NAG numbers.
namespace nag {
inline constexpr std::uint8_t kGood = 1;           // !
inline constexpr std::uint8_t kMistake = 2;        // ?
inline constexpr std::uint8_t kBrilliant = 3;      // !!
inline constexpr std::uint8_t kBlunder = 4;        // ??
inline constexpr std::uint8_t kInteresting = 5;    // !?
inline constexpr std::uint8_t kDubious = 6;        // ?!
inline constexpr std::uint8_t kEqual = 10;         // =
inline constexpr std::uint8_t kUnclear = 13;       // ∞
inline constexpr std::uint8_t kWhiteSlightly = 14; // ⩲
inline constexpr std::uint8_t kBlackSlightly = 15; // ⩱
inline constexpr std::uint8_t kWhiteBetter = 16;   // ±
inline constexpr std::uint8_t kBlackBetter = 17;   // ∓
inline constexpr std::uint8_t kWhiteWinning = 18;  // +−
inline constexpr std::uint8_t kBlackWinning = 19;  // −+
inline constexpr std::uint8_t kNovelty = 146;      // N
} // namespace nag

} // namespace kiebitz::core
