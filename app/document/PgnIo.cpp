#include "PgnIo.hpp"

#include <QFile>
#include <QObject>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>

namespace chessgui::app::pgnio {
namespace {

Loaded index(std::shared_ptr<PgnFile> file) {
    Loaded result;
    file->games = pgn::indexGames(file->text);
    if (!file->games.empty()) {
        const auto& entry = file->games.front();
        auto game = pgn::parseGame(std::string_view(file->text).substr(entry.offset, entry.length));
        if (game) {
            result.first = std::move(*game);
        } else {
            result.error = QString::fromStdString(game.error().message);
        }
    }
    result.file = std::move(file);
    return result;
}

} // namespace

QFuture<Loaded> load(const QString& path) {
    return QtConcurrent::run([path] {
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly)) {
            return Loaded{nullptr, std::nullopt, in.errorString()};
        }
        auto file = std::make_shared<PgnFile>();
        file->path = path;
        const QByteArray bytes = in.readAll();
        file->text.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        return index(std::move(file));
    });
}

QFuture<Loaded> loadText(std::string text) {
    return QtConcurrent::run([text = std::move(text)]() mutable {
        auto file = std::make_shared<PgnFile>();
        file->text = std::move(text);
        return index(std::move(file));
    });
}

QFuture<Parsed> parse(std::shared_ptr<const PgnFile> file, int index) {
    return QtConcurrent::run([file = std::move(file), index] {
        if (index < 0 || static_cast<std::size_t>(index) >= file->games.size()) {
            return Parsed{std::nullopt, QObject::tr("The file has no game %1.").arg(index + 1)};
        }
        const auto& entry = file->games[static_cast<std::size_t>(index)];
        auto game = pgn::parseGame(std::string_view(file->text).substr(entry.offset, entry.length));
        if (!game) {
            return Parsed{std::nullopt, QString::fromStdString(game.error().message)};
        }
        return Parsed{std::move(*game), {}};
    });
}

QFuture<QString> write(const QString& path, std::string text) {
    return QtConcurrent::run([path, text = std::move(text)] {
        QSaveFile out(path); // writes a temporary file and renames it into place on commit()
        if (!out.open(QIODevice::WriteOnly)) {
            return out.errorString();
        }
        if (out.write(text.data(), static_cast<qint64>(text.size())) !=
                static_cast<qint64>(text.size()) ||
            !out.commit()) {
            return out.errorString();
        }
        return QString();
    });
}

std::string replaceGame(const PgnFile& file, int index, std::string_view gameText) {
    const auto& entry = file.games.at(static_cast<std::size_t>(index));
    // The entry ends at the game's result; writeGame ends with a newline the old text has after.
    while (gameText.ends_with('\n')) {
        gameText.remove_suffix(1);
    }
    std::string result;
    result.reserve(file.text.size() + gameText.size());
    result.append(file.text, 0, entry.offset);
    result.append(gameText);
    result.append(file.text, entry.offset + entry.length);
    return result;
}

} // namespace chessgui::app::pgnio
