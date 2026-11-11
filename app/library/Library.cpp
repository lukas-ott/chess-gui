#include "Library.hpp"

#include "kiebitz/library/index_cache.hpp"

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
#include <mutex>
#include <unordered_map>

namespace kiebitz::app {

/// The index cache, opened the first time a scan needs it. That happens on a worker thread, so
/// opening the database never holds up the window.
struct LibraryCache {
    QString path;
    std::once_flag opened;
    std::unique_ptr<library::IndexCache> cache;

    /// The cache, or null if there is none (no path, or it couldn't be opened).
    library::IndexCache* get() {
        std::call_once(opened, [this] {
            if (path.isEmpty()) {
                return;
            }
            QDir().mkpath(QFileInfo(path).absolutePath());
            if (auto result = library::IndexCache::open(path.toStdString())) {
                cache = std::move(*result);
            } else {
                qWarning("%s", result.error().message.c_str());
            }
        });
        return cache.get();
    }
};

namespace {

library::FileStamp stampOf(const LibraryFile& file) {
    return {file.size, file.modified.toMSecsSinceEpoch()};
}

/// A file's index without its text, as the Library keeps it.
std::shared_ptr<PgnFile> indexOnly(const QString& path, std::vector<pgn::GameEntry> games) {
    auto file = std::make_shared<PgnFile>();
    file->path = path;
    file->games = std::move(games);
    file->hasText = false;
    return file;
}

struct Listing {
    std::vector<LibraryFile> files;
    qint64 milliseconds = 0;
};

/// Runs on a worker thread: lists the folder (names, sizes, dates; no file is read, so it is
/// quick even when iCloud has moved the files off the Mac). Unchanged files keep their index
/// from `known`, or get it from the cache; the others get none yet.
Listing list(const QString& folder, const std::vector<LibraryFile>& known,
             LibraryCache& cacheHolder) {
    library::IndexCache* cache = cacheHolder.get();
    QElapsedTimer timer;
    timer.start();
    std::unordered_map<QString, const LibraryFile*> previous;
    for (const auto& file : known) {
        previous.emplace(file.path, &file);
    }
    Listing result;
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
        } else if (cache != nullptr) {
            if (auto games = cache->find(file.path.toStdString(), stampOf(file))) {
                file.pgn = indexOnly(file.path, std::move(*games)); // unchanged since last time
            }
        }
        result.files.push_back(std::move(file));
    }
    if (cache != nullptr) { // forget files that are gone, so the cache doesn't grow forever
        std::vector<std::string> paths;
        paths.reserve(result.files.size());
        for (const auto& file : result.files) {
            paths.push_back(file.path.toStdString());
        }
        cache->forgetOthers(folder.toStdString(), paths);
    }
    std::sort(result.files.begin(), result.files.end(), [](const auto& a, const auto& b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    result.milliseconds = timer.elapsed();
    return result;
}

/// A file to read, and which version of it the listing saw.
struct ReadJob {
    QString path;
    library::FileStamp stamp;
};

/// One file read and indexed (on a worker thread, several at a time).
struct ReadFile {
    QString path;
    std::shared_ptr<PgnFile> pgn;
};

ReadFile readFile(const ReadJob& job, const std::atomic<bool>& cancel, LibraryCache& cacheHolder) {
    if (cancel) {
        return {job.path, nullptr};
    }
    QFile in(job.path);
    if (!in.open(QIODevice::ReadOnly)) {
        return {job.path, indexOnly(job.path, {})};
    }
    const QByteArray bytes = in.readAll();
    auto games = pgn::indexGames(std::string_view(bytes.constData(), bytes.size()));
    // Stored under the version the listing saw: if the file changed in between, its stamp is
    // newer by the next scan, which then simply reads it again.
    if (library::IndexCache* cache = cacheHolder.get()) {
        cache->store(job.path.toStdString(), job.stamp, games);
    }
    return {job.path, indexOnly(job.path, std::move(games))};
}

} // namespace

Library::~Library() {
    *m_cancel = true;
}

Library::Library(QObject* parent) : QObject(parent) {
    m_progressTimer.setSingleShot(true);
    m_progressTimer.setInterval(200);
    connect(&m_progressTimer, &QTimer::timeout, this, &Library::progress);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250);
    connect(&m_debounce, &QTimer::timeout, this, &Library::rescan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce,
            qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
}

QString Library::defaultCacheFile() {
    QString folder = qEnvironmentVariable("KIEBITZ_SETTINGS_DIR"); // tests: never the real one
    if (folder.isEmpty()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    }
    return QDir(folder).filePath(QStringLiteral("library.sqlite"));
}

void Library::setCacheFile(const QString& path) {
    m_cache = std::make_shared<LibraryCache>();
    m_cache->path = path;
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
        new QFutureWatcher<Listing>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<Listing>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_generation) { // the folder was changed meanwhile
            m_scanning = false;
            rescan();
            return;
        }
        Listing listing = watcher->result();
        // Nothing new (the watcher also fires for changes that aren't PGN files): leave the
        // views alone, so nothing is rebuilt under the user's hands.
        const bool same = m_reportedGeneration == generation &&
                          std::equal(m_files.begin(), m_files.end(), listing.files.begin(),
                                     listing.files.end(), [](const auto& a, const auto& b) {
                                         return a.path == b.path && a.pgn == b.pgn;
                                     });
        m_files = std::move(listing.files);
        watch();
        if (same) {
            m_scanning = false;
            if (m_rescanQueued) {
                m_rescanQueued = false;
                rescan();
            }
            return;
        }
        readFiles(generation, listing.milliseconds);
    });
    watcher->setFuture(QtConcurrent::run([folder = m_folder, known = m_files, cache = m_cache] {
        LibraryCache none;
        return list(folder, known, cache ? *cache : none);
    }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

void Library::readFiles(int generation, qint64 listedAfterMs) {
    QList<ReadJob> toRead;
    for (const auto& file : m_files) {
        if (!file.pgn) {
            toRead << ReadJob{file.path, stampOf(file)};
        }
    }
    QElapsedTimer timer;
    timer.start();
    const auto finish = [this, generation, listedAfterMs, timer] {
        m_reading = 0;
        m_progressTimer.stop();
        m_scanning = false;
        if (generation != m_generation) {
            rescan();
            return;
        }
        m_reportedGeneration = generation;
        int games = 0;
        for (const auto& file : m_files) {
            games += static_cast<int>(file.pgn ? file.pgn->games.size() : 0);
        }
        emit scanned(static_cast<int>(m_files.size()), games, listedAfterMs + timer.elapsed());
        if (m_rescanQueued) {
            m_rescanQueued = false;
            rescan();
        }
    };
    if (toRead.isEmpty()) {
        finish();
        return;
    }
    // The list first, so the Library shows the files at once; their games follow as each file
    // is read (iCloud may have to download them first).
    m_reading = static_cast<int>(toRead.size());
    emit progress();
    auto* watcher =
        new QFutureWatcher<ReadFile>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<ReadFile>::resultReadyAt, this,
            [this, watcher, generation](int index) {
                if (generation != m_generation) {
                    return;
                }
                const ReadFile read = watcher->resultAt(index);
                for (auto& file : m_files) {
                    if (file.path == read.path && !file.pgn) {
                        file.pgn = read.pgn;
                        --m_reading;
                        break;
                    }
                }
                if (!m_progressTimer.isActive()) {
                    m_progressTimer.start();
                }
            });
    connect(watcher, &QFutureWatcher<ReadFile>::finished, this, [watcher, finish] {
        watcher->deleteLater();
        finish();
    });
    watcher->setFuture(
        QtConcurrent::mapped(toRead, [cancel = m_cancel, cache = m_cache](const ReadJob& job) {
            LibraryCache none;
            return readFile(job, *cancel, cache ? *cache : none);
        }));
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

} // namespace kiebitz::app
