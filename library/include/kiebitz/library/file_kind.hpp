#pragma once

#include "kiebitz/pgn/pgn.hpp"

#include <optional>
#include <span>
#include <string_view>

/// What kind of file a PGN is, as En Croissant has it: games, a repertoire, a tournament,
/// puzzles or anything else. Stored in the file itself, as a header in its games
/// ([FileType "Repertoire"]), so it travels with the file.
namespace kiebitz::library {

enum class FileKind : std::uint8_t { Game, Repertoire, Tournament, Puzzle, Other };

/// The header that holds it.
inline constexpr std::string_view kFileTypeHeader = "FileType";

/// "Game", "Repertoire", ... (the header's value).
[[nodiscard]] std::string_view nameOf(FileKind kind) noexcept;
/// The kind a header value names (any case), if it names one.
[[nodiscard]] std::optional<FileKind> kindFromName(std::string_view name) noexcept;

/// A file's kind from its games' headers: the first game with [FileType] decides; files
/// marked by an earlier Kiebitz ([Repertoire "..."]) are repertoires; everything else is games.
[[nodiscard]] FileKind kindOf(std::span<const pgn::GameEntry> games);

} // namespace kiebitz::library
