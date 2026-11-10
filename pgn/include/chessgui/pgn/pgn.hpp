#pragma once

#include "chessgui/core/game.hpp"
#include "chessgui/core/result.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// Reading and writing PGN (Portable Game Notation), the standard text format for chess games.
///
/// A PGN file holds any number of games. Opening one is done in two steps, so big files open fast:
///   1. indexGames() finds where each game starts and ends and reads only its headers (for the
///      game list). This is a quick scan with no chess logic.
///   2. parseGame() reads one game's moves when the user opens it.
/// The byte offsets from step 1 also let a changed game be saved by replacing just its text.
namespace chessgui::pgn {

using Headers = std::vector<std::pair<std::string, std::string>>;

/// Where one game sits in a PGN text, plus its headers.
struct GameEntry {
    std::size_t offset = 0; ///< first byte of the game (its first header, or its first move)
    std::size_t length = 0; ///< bytes up to the end of its result token (or the next game)
    Headers headers;

    /// A header's value, or "" if the game doesn't have it.
    [[nodiscard]] std::string_view header(std::string_view name) const noexcept;
};

/// Finds every game in a PGN text and reads its headers. Never fails: text that isn't a game
/// is skipped.
[[nodiscard]] std::vector<GameEntry> indexGames(std::string_view text);

/// Reads one game: headers, moves, variations, comments, NAGs, arrows and square marks.
/// `text` is one game (e.g. text.substr(entry.offset, entry.length)); anything after its result
/// is ignored. Errors name the line and move, e.g. "Line 12, move 17... Qxe4: not a legal move".
[[nodiscard]] core::Result<core::Game> parseGame(std::string_view text);

/// Reads every game in a text (indexGames + parseGame). For tests and small files.
[[nodiscard]] core::Result<std::vector<core::Game>> parseAll(std::string_view text);

/// Writes a game as standard PGN: headers, a blank line, then the moves wrapped at 80 columns,
/// with variations, comments (arrows and marks as [%cal]/[%csl]) and NAGs as $n.
[[nodiscard]] std::string writeGame(const core::Game& game);

} // namespace chessgui::pgn
