#include "DatabaseManager.hpp"

#include "kiebitz/gamedb/import.hpp"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <mutex>

namespace kiebitz::app {
namespace {

/// The command that writes an archive's PGN to its output, or nothing for plain PGN.
std::optional<std::pair<QString, QStringList>> unpacker(const QString& path) {
    const QString lower = path.toLower();
    if (lower.endsWith(u".7z") || lower.endsWith(u".zip")) {
        // macOS's bsdtar reads both, and writes the PGN files inside to its output.
        return std::pair{QStringLiteral("/usr/bin/tar"),
                         QStringList{QStringLiteral("-xOf"), path, QStringLiteral("--include"),
                                     QStringLiteral("*.pgn"), QStringLiteral("--include"),
                                     QStringLiteral("*.PGN")}};
    }
    if (lower.endsWith(u".gz")) {
        return std::pair{QStringLiteral("/usr/bin/gzip"), QStringList{QStringLiteral("-dc"), path}};
    }
    if (lower.endsWith(u".bz2")) {
        return std::pair{QStringLiteral("/usr/bin/bzip2"),
                         QStringList{QStringLiteral("-dc"), path}};
    }
    return std::nullopt;
}

qint64 folderSize(const QString& folder) {
    qint64 bytes = 0;
    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        bytes += it.nextFileInfo().size();
    }
    return bytes;
}

/// A folder name from a database name: letters, digits, spaces and dashes.
QString folderName(const QString& name) {
    QString result;
    for (const QChar c : name) {
        result += c.isLetterOrNumber() || c == u' ' || c == u'-' ? c : QChar(u'-');
    }
    result = result.trimmed();
    return result.isEmpty() ? QStringLiteral("Database") : result;
}

} // namespace

DatabaseManager::DatabaseManager(const QString& root, QObject* parent)
    : QObject(parent), m_root(root.isEmpty() ? defaultRoot() : root) {
    // Imports that never finished (the app was quit or crashed meanwhile) left a ".part"
    // folder: nothing runs yet, so they can go.
    const auto parts =
        QDir(m_root).entryInfoList({QStringLiteral("*.part")}, QDir::Dirs | QDir::NoDotAndDotDot);
    if (!parts.isEmpty()) {
        (void)QtConcurrent::run([parts] {
            for (const QFileInfo& part : parts) {
                QDir(part.absoluteFilePath()).removeRecursively();
            }
        });
    }
    refresh();
}

DatabaseManager::~DatabaseManager() {
    // Running imports stop; their half-written folders are removed by them.
    for (const Running& running : m_running) {
        running.cancel->store(true);
    }
    for (Running& running : m_running) {
        running.future.waitForFinished();
    }
}

QString DatabaseManager::defaultRoot() {
    QString folder = qEnvironmentVariable("KIEBITZ_SETTINGS_DIR"); // tests: never the real one
    if (folder.isEmpty()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    return QDir(folder).filePath(QStringLiteral("Databases"));
}

void DatabaseManager::refresh() {
    const int request = ++m_refresh;
    using Found = std::vector<InstalledDatabase>;
    // Owned by this object and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher =
        new QFutureWatcher<Found>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<Found>::finished, this, [this, watcher, request] {
        watcher->deleteLater();
        if (request != m_refresh) {
            return;
        }
        m_databases = watcher->result();
        emit changed();
        // Databases from before the explorer showed ratings get them once, in the background.
        // The watchers are owned by the manager and deleted when done (deleteLater).
        // NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
        for (const InstalledDatabase& database : m_databases) {
            if ((database.db->hasRatings() && database.db->hasAttributes()) ||
                m_upgrading.contains(database.folder)) {
                continue;
            }
            m_upgrading.insert(database.folder);
            auto* upgrade = new QFutureWatcher<std::string_view>(this);
            connect(upgrade, &QFutureWatcher<std::string_view>::finished, this, [this, upgrade] {
                upgrade->deleteLater();
                refresh();
            });
            upgrade->setFuture(QtConcurrent::run(
                [folder = database.folder.toStdString()] { return gamedb::upgrade(folder); }));
        }
        // NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)
    });
    watcher->setFuture(QtConcurrent::run([root = m_root] {
        Found found;
        QDir(root).mkpath(QStringLiteral("."));
        const auto entries = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries) {
            if (entry.fileName().endsWith(u".part")) {
                continue; // an import still running, or one that never finished
            }
            auto opened = gamedb::Database::open(entry.absoluteFilePath().toStdString());
            if (!opened.ok()) {
                continue;
            }
            found.push_back({entry.absoluteFilePath(),
                             std::shared_ptr<const gamedb::Database>(std::move(*opened)),
                             folderSize(entry.absoluteFilePath())});
        }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
            return QString::localeAwareCompare(QString::fromStdString(a.db->info().name),
                                               QString::fromStdString(b.db->info().name)) < 0;
        });
        return found;
    }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

qint64 DatabaseManager::spaceNeeded(const QString& path) {
    // A database takes about a third of its PGN; archives hold PGN packed about 5 to 1.
    const qint64 size = QFileInfo(path).size();
    return unpacker(path) ? size * 2 : (size / 3) + (64LL * 1024 * 1024);
}

qint64 DatabaseManager::spaceFree() const {
    QDir().mkpath(m_root);
    return QStorageInfo(m_root).bytesAvailable();
}

int DatabaseManager::importFile(const QString& path, const QString& name, const QString& source) {
    const int id = m_nextJob++;
    m_jobs.push_back({id, name, tr("Starting"), -1.0, 0});
    emit jobChanged(id);

    // A folder name that isn't taken.
    const QDir root(m_root);
    root.mkpath(QStringLiteral("."));
    QString folder = root.filePath(folderName(name));
    for (int n = 2; QFileInfo::exists(folder) || QFileInfo::exists(folder + u".part"); ++n) {
        folder = root.filePath(folderName(name) + QStringLiteral(" %1").arg(n));
    }
    const QString part = folder + QStringLiteral(".part");

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    QPointer<DatabaseManager> self(this);
    const auto post = [self, id](const QString& phase, double fraction, quint64 games) {
        QMetaObject::invokeMethod(
            self,
            [self, id, phase, fraction, games] {
                if (self) {
                    self->update(id, phase, fraction, games);
                }
            },
            Qt::QueuedConnection);
    };
    const QFuture<void> future = QtConcurrent::run([=] {
        const auto fail = [&](const QString& error) {
            QDir(part).removeRecursively();
            QMetaObject::invokeMethod(
                self,
                [self, id, error] {
                    if (self) {
                        self->finished(id, error);
                    }
                },
                Qt::QueuedConnection);
        };
        QDir().mkpath(part);
        gamedb::DatabaseInfo info;
        info.name = name.toStdString();
        info.source = (source.isEmpty() ? QFileInfo(path).fileName() : source).toStdString();
        auto writer = gamedb::DatabaseWriter::create(part.toStdString(), info);
        if (!writer.ok()) {
            fail(QString::fromStdString(writer.error().message));
            return;
        }

        // The input: the file itself, or what an unpacker writes.
        QFile file(path);
        QProcess process;
        const auto command = unpacker(path);
        const qint64 total = QFileInfo(path).size();
        if (command) {
            process.start(command->first, command->second);
            if (!process.waitForStarted()) {
                fail(tr("The archive couldn't be unpacked."));
                return;
            }
        } else if (!file.open(QIODevice::ReadOnly)) {
            fail(tr("%1 couldn't be read.").arg(QFileInfo(path).fileName()));
            return;
        }
        const auto read = [&](char* buffer, std::size_t size) -> std::size_t {
            const auto max = static_cast<qint64>(size);
            if (!command) {
                const qint64 got = file.read(buffer, max);
                return got > 0 ? static_cast<std::size_t>(got) : 0;
            }
            while (!cancel->load()) {
                const qint64 got = process.read(buffer, max);
                if (got > 0) {
                    return static_cast<std::size_t>(got);
                }
                if (process.state() == QProcess::NotRunning) {
                    return 0;
                }
                process.waitForReadyRead(200);
            }
            return 0;
        };
        QElapsedTimer sinceReport;
        sinceReport.start();
        gamedb::ImportOptions options;
        options.cancel = cancel.get();
        options.progress = [&](const gamedb::ImportProgress& counts) {
            if (sinceReport.elapsed() < 200) {
                return;
            }
            sinceReport.restart();
            post(tr("Reading games"),
                 command ? -1.0 : static_cast<double>(counts.bytes) / static_cast<double>(total),
                 counts.games);
        };
        const gamedb::ImportResult result = gamedb::importPgn(**writer, read, options);
        if (command) {
            if (result.cancelled) {
                process.kill();
            }
            process.waitForFinished(-1);
        }
        if (result.cancelled) {
            fail(QStringLiteral("cancelled"));
            return;
        }
        if (result.counts.games == 0) {
            fail(command && process.exitCode() != 0
                     ? tr("The archive couldn't be unpacked: %1")
                           .arg(QString::fromUtf8(process.readAllStandardError()).trimmed())
                     : tr("No games were found in %1.").arg(QFileInfo(path).fileName()));
            return;
        }
        const quint64 games = result.counts.games;
        const std::string error =
            (*writer)->finish([&](double done) { post(tr("Indexing"), done, games); });
        writer->reset(); // closes the files before the folder is renamed
        if (!error.empty()) {
            fail(QString::fromStdString(error));
            return;
        }
        if (!QDir().rename(part, folder)) {
            fail(tr("The database couldn't be put in place."));
            return;
        }
        QMetaObject::invokeMethod(
            self,
            [self, id] {
                if (self) {
                    self->finished(id, {});
                }
            },
            Qt::QueuedConnection);
    });
    m_running.push_back({id, cancel, future});
    return id;
}

void DatabaseManager::update(int id, const QString& phase, double fraction, quint64 games) {
    for (DatabaseJob& job : m_jobs) {
        if (job.id == id) {
            job.phase = phase;
            job.fraction = fraction;
            job.games = games;
            emit jobChanged(id);
            return;
        }
    }
}

void DatabaseManager::finished(int id, const QString& error) {
    std::erase_if(m_jobs, [id](const DatabaseJob& job) { return job.id == id; });
    std::erase_if(m_running, [id](const Running& running) { return running.id == id; });
    emit jobFinished(id, error);
    refresh();
}

void DatabaseManager::cancel(int job) {
    for (const Running& running : m_running) {
        if (running.id == job) {
            running.cancel->store(true);
        }
    }
}

QString DatabaseManager::remove(const QString& folder) {
    if (!QFileInfo(folder).absoluteFilePath().startsWith(QDir(m_root).absolutePath())) {
        return tr("That isn't one of Kiebitz's databases.");
    }
    if (!QFile::moveToTrash(folder) && !QDir(folder).removeRecursively()) {
        return tr("The database couldn't be removed.");
    }
    refresh();
    return {};
}

} // namespace kiebitz::app
