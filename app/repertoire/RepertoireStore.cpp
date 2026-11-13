#include "RepertoireStore.hpp"

#include "document/PgnIo.hpp"
#include "kiebitz/repertoire/gaps.hpp"
#include "library/Library.hpp"

#include <QFile>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <functional>
#include <utility>

namespace kiebitz::app {
namespace {

std::size_t hashOf(const std::string& text) {
    return std::hash<std::string>{}(text);
}

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// Runs `done` with the result on the UI thread (the watcher deletes itself).
template <typename T, typename Done>
void whenDone(QObject* context, const QFuture<T>& future, const Done& done) {
    auto* watcher =
        new QFutureWatcher<T>(context); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    QObject::connect(watcher, &QFutureWatcher<T>::finished, context, [watcher, done] {
        watcher->deleteLater();
        done(watcher->result());
    });
    watcher->setFuture(future);
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

void parseGames(RepertoireFile& file) {
    file.games.clear();
    file.games.reserve(file.file->games.size());
    for (const auto& entry : file.file->games) {
        auto game =
            pgn::parseGame(std::string_view(file.file->text).substr(entry.offset, entry.length));
        file.games.push_back(game ? std::move(*game) : core::Game()); // indexes keep matching
    }
    file.textHash = hashOf(file.file->text);
}

/// Reads and parses one file (on a worker thread).
RepertoireFile readFile(const QString& path) {
    RepertoireFile result;
    result.file = std::make_shared<PgnFile>();
    result.file->path = path;
    QFile in(path);
    if (in.open(QIODevice::ReadOnly)) {
        const QByteArray bytes = in.readAll();
        result.file->text.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    }
    result.file->games = pgn::indexGames(result.file->text);
    parseGames(result);
    return result;
}

repertoire::Tree treeOf(core::Color side, const std::vector<RepertoireFile>& files) {
    repertoire::Tree tree(side);
    for (std::size_t f = 0; f < files.size(); ++f) {
        for (std::size_t g = 0; g < files[f].games.size(); ++g) {
            tree.add(files[f].games[g], static_cast<int>(f), static_cast<int>(g));
        }
    }
    return tree;
}

} // namespace

RepertoireStore::RepertoireStore(Library& library, QObject* parent)
    : QObject(parent), m_library(library) {
    connect(&m_library, &Library::scanned, this, &RepertoireStore::libraryChanged);
    connect(&m_library, &Library::progress, this, &RepertoireStore::libraryChanged);
}

// ---- What a file is ----------------------------------------------------------------------------

library::FileKind RepertoireStore::kindOf(const PgnFile& file) {
    return library::kindOf(file.games);
}

std::optional<core::Color> RepertoireStore::orientationOf(const PgnFile& file) {
    int white = 0;
    int black = 0;
    for (const auto& game : file.games) {
        const QString side = toQString(game.header("Orientation"));
        white += side.compare(u"white", Qt::CaseInsensitive) == 0 ? 1 : 0;
        black += side.compare(u"black", Qt::CaseInsensitive) == 0 ? 1 : 0;
    }
    if (white == black) {
        return std::nullopt;
    }
    return white > black ? core::Color::White : core::Color::Black;
}

core::Color RepertoireStore::sideOf(const PgnFile& file) {
    return orientationOf(file).value_or(core::Color::White);
}

const RepertoireFile* RepertoireStore::builtFile(const QString& path) const {
    for (const Side& side : m_sides) {
        if (!side.built) {
            continue;
        }
        for (const RepertoireFile& file : side.built->files) {
            if (file.file && file.file->path == path) {
                return &file;
            }
        }
    }
    return nullptr;
}

const BuiltRepertoire* RepertoireStore::get(core::Color side) const {
    return sideData(side).built.get();
}

const QStringList& RepertoireStore::files(core::Color side) const {
    return sideData(side).files;
}

bool RepertoireStore::isBuilding(core::Color side) const {
    return sideData(side).building;
}

std::shared_ptr<PgnFile> RepertoireStore::sharedFile(const QString& path) const {
    for (const Side& side : m_sides) {
        if (!side.built) {
            continue;
        }
        for (const auto& file : side.built->files) {
            if (file.file->path == path) {
                return file.file;
            }
        }
    }
    return nullptr;
}

QStringList RepertoireStore::filesWith(core::Color side, std::uint64_t position) const {
    QStringList paths;
    const BuiltRepertoire* built = get(side);
    const repertoire::Entry* entry = built != nullptr ? built->tree.find(position) : nullptr;
    if (entry == nullptr) {
        return paths;
    }
    for (const repertoire::Source& source : entry->sources) {
        const QString& path = built->files[static_cast<std::size_t>(source.file)].file->path;
        if (!paths.contains(path)) {
            paths << path;
        }
    }
    return paths;
}

// ---- Building ----------------------------------------------------------------------------------

void RepertoireStore::libraryChanged() {
    std::array<QStringList, 2> files;
    std::array<QStringList, 2> stamps;
    for (const auto& file : m_library.files()) {
        if (!file.pgn || kindOf(*file.pgn) != library::FileKind::Repertoire) {
            continue; // not read yet, or not a repertoire
        }
        const std::size_t i = sideOf(*file.pgn) == core::Color::White ? 0 : 1;
        files[i] << file.path;
        stamps[i] << QStringLiteral("%1|%2|%3")
                         .arg(file.path, QString::number(file.size),
                              QString::number(file.modified.toMSecsSinceEpoch()));
    }
    for (const core::Color side : {core::Color::White, core::Color::Black}) {
        const std::size_t i = side == core::Color::White ? 0 : 1;
        Side& data = sideData(side);
        const bool listChanged = data.files != files[i];
        data.files = files[i];
        if (stamps[i] != data.builtFrom && m_pendingWrites == 0) {
            data.builtFrom = stamps[i];
            rebuild(side);
        } else if (listChanged) {
            emit changed(side);
        }
    }
}

void RepertoireStore::rebuild(core::Color side) {
    Side& data = sideData(side);
    const int generation = ++data.generation;
    if (data.files.isEmpty()) {
        data.built.reset();
        data.building = false;
        emit changed(side);
        return;
    }
    data.building = true;
    emit changed(side);
    const QStringList paths = data.files;
    whenDone(this, QtConcurrent::run([side, paths] {
                 auto built = std::make_shared<BuiltRepertoire>();
                 built->side = side;
                 for (const QString& path : paths) {
                     built->files.push_back(readFile(path));
                 }
                 built->tree = treeOf(side, built->files);
                 return built;
             }),
             [this, side, generation](std::shared_ptr<BuiltRepertoire> built) {
                 Side& current = sideData(side);
                 if (generation != current.generation) {
                     return; // an edit or a newer build came after
                 }
                 // Keep the file objects tabs may share: same identity, new text.
                 for (auto& file : built->files) {
                     if (const auto shared = sharedFile(file.file->path)) {
                         shared->text = std::move(file.file->text);
                         shared->games = std::move(file.file->games);
                         shared->hasText = true;
                         file.file = shared;
                     }
                 }
                 current.built = std::move(built);
                 current.building = false;
                 emit changed(side);
             });
}

void RepertoireStore::catchUp(BuiltRepertoire& built) {
    bool changedAny = false;
    for (auto& file : built.files) {
        if (hashOf(file.file->text) != file.textHash) {
            parseGames(file);
            changedAny = true;
        }
    }
    if (changedAny) {
        built.tree = treeOf(built.side, built.files);
    }
}

void RepertoireStore::loadOpenings() {
    if (m_openings || m_openingsLoading) {
        return;
    }
    m_openingsLoading = true;
    whenDone(this, QtConcurrent::run([] {
                 QFile in(QStringLiteral(":/openings/openings.tsv"));
                 const QByteArray bytes =
                     in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray();
                 return std::make_shared<const repertoire::OpeningNames>(
                     repertoire::OpeningNames::fromTsv(std::string_view(
                         bytes.constData(), static_cast<std::size_t>(bytes.size()))));
             }),
             [this](std::shared_ptr<const repertoire::OpeningNames> names) {
                 m_openings = std::move(names);
                 m_openingsLoading = false;
                 emit openingsReady();
             });
}

// ---- Editing -----------------------------------------------------------------------------------

void RepertoireStore::write(const std::shared_ptr<PgnFile>& file) {
    ++m_pendingWrites;
    whenDone(this, pgnio::write(file->path, file->text), [this](const QString& error) {
        --m_pendingWrites;
        if (!error.isEmpty()) {
            emit failed(error);
        }
    });
}

RepertoireStore::Added RepertoireStore::addLine(core::Color side, std::span<const core::Move> line,
                                                bool allowSecondAnswer) {
    Added result;
    if (m_beforeEdit) {
        m_beforeEdit();
    }
    Side& data = sideData(side);
    if (!data.built || data.built->files.empty()) {
        return result; // NoFile
    }
    BuiltRepertoire& built = *data.built;
    catchUp(built);
    const repertoire::Book book = built.tree.follow(core::Position(), line);
    result.ply = book.plies;
    if (std::cmp_equal(book.plies, line.size())) {
        result.outcome = Outcome::AlreadyThere;
        return result;
    }
    if (book.byUs && !book.expected.empty() && !allowSecondAnswer) {
        result.outcome = Outcome::Conflict;
        result.expected = book.expected;
        return result;
    }

    // Into the game that has the line's last known position.
    core::Position position;
    for (int k = 0; k < book.plies; ++k) {
        position.play(line[static_cast<std::size_t>(k)]);
    }
    repertoire::Source source{0, 0, core::Game::root()};
    if (const repertoire::Entry* entry = built.tree.find(position.hash());
        entry != nullptr && !entry->sources.empty()) {
        source = entry->sources.front();
    }
    RepertoireFile& target = built.files[static_cast<std::size_t>(source.file)];
    if (target.games.empty()) { // an empty file: start its first game
        core::Game game;
        game.setHeader(library::kFileTypeHeader, library::nameOf(library::FileKind::Repertoire));
        game.setHeader("Orientation", side == core::Color::White ? "white" : "black");
        target.file->text +=
            (target.file->text.empty() ? std::string() : std::string("\n")) + pgn::writeGame(game);
        target.file->games = pgn::indexGames(target.file->text);
        target.games.push_back(std::move(game));
        source = {source.file, 0, core::Game::root()};
    }
    core::Game& game = target.games[static_cast<std::size_t>(source.game)];
    core::NodeId node = source.node;
    for (auto k = static_cast<std::size_t>(book.plies); k < line.size(); ++k) {
        node = game.addMove(node, position, line[k]);
        position.play(line[k]);
    }
    auto text = pgnio::replaceGame(*target.file, source.game, pgn::writeGame(game));
    if (!text) {
        emit failed(tr("Couldn't add the line: the file changed."));
        return result;
    }
    target.file->text = std::move(*text);
    target.file->games = pgn::indexGames(target.file->text);
    target.textHash = hashOf(target.file->text);
    ++data.generation; // a build from disk that is still running is older than this
    built.tree = treeOf(side, built.files);
    write(target.file);
    result.outcome = Outcome::Added;
    result.path = target.file->path;
    emit fileEdited(target.file->path, target.file);
    emit changed(side);
    return result;
}

// ---- Where a file starts ----

namespace {

repertoire::Tree fileTree(const RepertoireFile& file) {
    repertoire::Tree tree(file.file ? RepertoireStore::sideOf(*file.file) : core::Color::White);
    for (std::size_t i = 0; i < file.games.size(); ++i) {
        tree.add(file.games[i], 0, static_cast<int>(i));
    }
    return tree;
}

} // namespace

std::vector<core::Move> RepertoireStore::trunkOf(const RepertoireFile& file) {
    return repertoire::trunk(fileTree(file));
}

std::optional<std::vector<core::Move>> RepertoireStore::chosenStartOf(const RepertoireFile& file) {
    for (const core::Game& game : file.games) {
        for (const auto& [name, value] : game.headers()) {
            if (name == kStartHeader) {
                return repertoire::parseLine(value);
            }
        }
    }
    return std::nullopt;
}

std::vector<core::Move> RepertoireStore::startOf(const RepertoireFile& file) {
    if (auto chosen = chosenStartOf(file)) {
        return std::move(*chosen);
    }
    return trunkOf(file);
}

void RepertoireStore::setStart(const QString& path,
                               const std::optional<std::vector<core::Move>>& line) {
    std::optional<std::string> value;
    if (line) {
        value = repertoire::lineText(*line);
    }
    // NOLINTNEXTLINE(bugprone-exception-escape): runs on a worker; out of memory ends the app
    editHeaders({path}, [value](const std::string& text) {
        return pgn::setHeaderInAllGames(text, kStartHeader, value);
    });
}

// ---- File types --------------------------------------------------------------------------------

void RepertoireStore::setKind(const QStringList& paths, library::FileKind kind,
                              std::optional<core::Color> side) {
    const std::string type(library::nameOf(kind));
    std::optional<std::string> orientation;
    if (kind == library::FileKind::Repertoire && side) {
        orientation = *side == core::Color::White ? "white" : "black";
    }
    // NOLINTNEXTLINE(bugprone-exception-escape): runs on a worker; out of memory ends the app
    editHeaders(paths, [type, orientation](const std::string& text) {
        std::string result = pgn::setHeaderInAllGames(text, library::kFileTypeHeader, type);
        result = pgn::setHeaderInAllGames(result, "Repertoire", std::nullopt); // older marking
        if (orientation) {
            result = pgn::setHeaderInAllGames(result, "Orientation", *orientation);
        }
        return result;
    });
}

void RepertoireStore::editHeaders(const QStringList& paths,
                                  const std::function<std::string(const std::string&)>& change) {
    if (m_beforeEdit) {
        m_beforeEdit();
    }
    // Start from the text the app already has (a tab's unsaved-to-disk edits included).
    std::vector<std::pair<QString, std::optional<std::string>>> jobs;
    for (const QString& path : paths) {
        std::shared_ptr<PgnFile> current = sharedFile(path);
        if (!current && m_openText) {
            current = m_openText(path);
        }
        jobs.emplace_back(path, current && current->hasText ? std::optional(current->text)
                                                            : std::nullopt);
    }
    ++m_pendingWrites;
    whenDone(this, QtConcurrent::run([jobs, change] {
                 std::vector<std::shared_ptr<PgnFile>> results;
                 for (const auto& [path, known] : jobs) {
                     auto file = std::make_shared<PgnFile>();
                     file->path = path;
                     if (known) {
                         file->text = *known;
                     } else {
                         QFile in(path);
                         if (!in.open(QIODevice::ReadOnly)) {
                             continue;
                         }
                         const QByteArray bytes = in.readAll();
                         file->text.assign(bytes.constData(),
                                           static_cast<std::size_t>(bytes.size()));
                     }
                     file->text = change(file->text);
                     file->games = pgn::indexGames(file->text);
                     results.push_back(std::move(file));
                 }
                 return results;
             }),
             [this](const std::vector<std::shared_ptr<PgnFile>>& files) {
                 --m_pendingWrites;
                 for (const auto& file : files) {
                     std::shared_ptr<PgnFile> target = file;
                     if (const auto shared = sharedFile(file->path)) { // keep what tabs share
                         shared->text = file->text;
                         shared->games = file->games;
                         target = shared;
                     }
                     write(target);
                     emit fileEdited(target->path, target);
                 }
             });
}

} // namespace kiebitz::app
