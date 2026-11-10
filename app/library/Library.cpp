#include "Library.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFutureWatcher>
#include <QSet>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <unordered_map>

namespace chessgui::app {
namespace {

struct ScanResult {
    std::vector<LibraryFile> files;
    qint64 milliseconds = 0;
};

/// Runs on a worker thread: lists the folder, then reads and indexes every new or changed file,
/// several at a time. `known` are the files from the last scan; unchanged ones are reused.
ScanResult scan(const QString& folder, const std::vector<LibraryFile>& known) {
    QElapsedTimer timer;
    timer.start();
    std::unordered_map<QString, const LibraryFile*> previous;
    for (const auto& file : known) {
        previous.emplace(file.path, &file);
    }

    ScanResult result;
    QDirIterator it(folder, {QStringLiteral("*.pgn"), QStringLiteral("*.PGN")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        LibraryFile file{info.absoluteFilePath(), QDir(folder).relativeFilePath(info.filePath()),
                         info.size(), info.lastModified(), nullptr};
        const auto old = previous.find(file.path);
        if (old != previous.end() && old->second->size == file.size &&
            old->second->modified == file.modified) {
            file.pgn = old->second->pgn; // unchanged: keep its index
        }
        result.files.push_back(std::move(file));
    }
    std::sort(result.files.begin(), result.files.end(), [](const auto& a, const auto& b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });

    // Read and index the files that need it, in parallel.
    QtConcurrent::blockingMap(result.files, [](LibraryFile& file) {
        if (file.pgn) {
            return;
        }
        auto pgnFile = std::make_shared<PgnFile>();
        pgnFile->path = file.path;
        QFile in(file.path);
        if (in.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = in.readAll();
            pgnFile->text.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
            pgnFile->games = pgn::indexGames(pgnFile->text);
        }
        file.pgn = std::move(pgnFile);
    });
    result.milliseconds = timer.elapsed();
    return result;
}

} // namespace

Library::Library(QObject* parent) : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250);
    connect(&m_debounce, &QTimer::timeout, this, &Library::rescan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce,
            qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
}

QString Library::defaultFolder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QCoreApplication::applicationName()); // named after the app
}

void Library::setFolder(const QString& folder) {
    // Without a slash at the end (the macOS folder panel adds one), so the folder has a name.
    m_folder = QDir::cleanPath(folder);
    QDir().mkpath(m_folder);
    m_files.clear();
    ++m_generation;
    rescan();
}

void Library::rescan() {
    if (m_folder.isEmpty()) {
        return;
    }
    if (m_scanning) {
        m_rescanQueued = true; // one more scan when this one is done
        return;
    }
    startScan();
}

void Library::startScan() {
    m_scanning = true;
    const int generation = m_generation;
    // Owned by this Library and deleted when done (deleteLater); see MainWindow's whenReady().
    auto* watcher =
        new QFutureWatcher<ScanResult>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<ScanResult>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        m_scanning = false;
        if (generation != m_generation) { // the folder was changed meanwhile
            rescan();
            return;
        }
        ScanResult result = watcher->result();
        // Nothing new (the watcher also fires for changes that aren't PGN files): leave the
        // views alone, so nothing is rebuilt under the user's hands.
        const bool same = m_reportedGeneration == generation &&
                          std::equal(m_files.begin(), m_files.end(), result.files.begin(),
                                     result.files.end(), [](const auto& a, const auto& b) {
                                         return a.path == b.path && a.pgn == b.pgn;
                                     });
        m_files = std::move(result.files);
        watch();
        if (same) {
            if (m_rescanQueued) {
                m_rescanQueued = false;
                rescan();
            }
            return;
        }
        m_reportedGeneration = generation;
        int games = 0;
        for (const auto& file : m_files) {
            games += static_cast<int>(file.pgn ? file.pgn->games.size() : 0);
        }
        emit scanned(static_cast<int>(m_files.size()), games, result.milliseconds);
        if (m_rescanQueued) {
            m_rescanQueued = false;
            rescan();
        }
    });
    watcher->setFuture(
        QtConcurrent::run([folder = m_folder, known = m_files] { return scan(folder, known); }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

void Library::watch() {
    // Watch the folder and its subfolders (new or removed files) and every file (edits). Only
    // the difference to what is already watched is applied: starting to watch a path again can
    // itself report a change, and then every scan would set off the next one.
    QStringList wanted{m_folder};
    QDirIterator dirs(m_folder, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (dirs.hasNext()) {
        wanted << dirs.next();
    }
    for (const auto& file : m_files) {
        wanted << file.path;
    }
    const QStringList watched = m_watcher.directories() + m_watcher.files();
    const QSet<QString> wantedSet(wanted.cbegin(), wanted.cend());
    const QSet<QString> watchedSet(watched.cbegin(), watched.cend());
    QStringList gone;
    QStringList added;
    for (const auto& path : watched) {
        if (!wantedSet.contains(path)) {
            gone << path;
        }
    }
    for (const auto& path : wanted) {
        if (!watchedSet.contains(path)) {
            added << path;
        }
    }
    if (!gone.isEmpty()) {
        m_watcher.removePaths(gone);
    }
    if (!added.isEmpty()) {
        m_watcher.addPaths(added);
    }
}

} // namespace chessgui::app
