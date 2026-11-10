#pragma once

#include "GameDocument.hpp"

#include <QFuture>
#include <QString>

#include <memory>
#include <optional>
#include <string>

/// File work for PGN documents. Everything that touches the disk or parses runs on a worker
/// thread (QtConcurrent) and hands its result back as a QFuture, so the window never freezes.
namespace kiebitz::app::pgnio {

struct Loaded {
    std::shared_ptr<PgnFile> file;   ///< null if the file couldn't be read
    std::optional<core::Game> first; ///< the first game, parsed (if there is one)
    QString error;                   ///< why reading or parsing failed
};

struct Parsed {
    std::optional<core::Game> game;
    QString error;
};

/// Reads a file, indexes its games and parses the first one.
[[nodiscard]] QFuture<Loaded> load(const QString& path);
/// Indexes PGN text that is already in memory (e.g. from the clipboard) and parses its first game.
[[nodiscard]] QFuture<Loaded> loadText(std::string text);
/// Parses one game of a file.
[[nodiscard]] QFuture<Parsed> parse(std::shared_ptr<const PgnFile> file, int index);
/// Writes text to a file atomically (a temporary file renamed into place). Returns an error
/// message, or an empty string on success.
[[nodiscard]] QFuture<QString> write(const QString& path, std::string text);

/// The file's text with one game's text replaced, for saving a game back into its file.
[[nodiscard]] std::string replaceGame(const PgnFile& file, int index, std::string_view gameText);

} // namespace kiebitz::app::pgnio
