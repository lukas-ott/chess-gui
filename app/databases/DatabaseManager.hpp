#pragma once

#include "kiebitz/gamedb/database.hpp"

#include <QFuture>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <vector>

namespace kiebitz::app {

/// A database on this Mac.
struct InstalledDatabase {
    QString folder;
    std::shared_ptr<const gamedb::Database> db;
    qint64 bytes = 0; ///< on disk
};

/// An import running in the background.
struct DatabaseJob {
    int id = 0;
    QString name;
    QString phase;          ///< "Reading games", "Indexing"
    double fraction = -1.0; ///< 0..1, or -1 when the end isn't known (an archive)
    quint64 games = 0;
};

/// The game databases (big reference collections: Lumbra's Gigabase, your own PGN archives).
/// Each is a gamedb::Database in its own folder under the app's data folder. Imports run in
/// the background: plain PGN is read as it is; .7z, .zip, .gz and .bz2 are unpacked on the fly
/// by the system's tools (nothing unpacked is written to disk). An import goes into
/// "<folder>.part" and is renamed when it is complete, so a cancelled or crashed import never
/// shows up as a database.
class DatabaseManager : public QObject {
    Q_OBJECT

public:
    /// `root`: where databases live (empty: the default).
    explicit DatabaseManager(const QString& root = {}, QObject* parent = nullptr);
    ~DatabaseManager() override;
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
    DatabaseManager(DatabaseManager&&) = delete;
    DatabaseManager& operator=(DatabaseManager&&) = delete;

    /// Application Support/…/Databases (tests: under KIEBITZ_SETTINGS_DIR).
    [[nodiscard]] static QString defaultRoot();
    [[nodiscard]] const QString& root() const { return m_root; }

    /// Looks at the folder again (in the background); changed() follows.
    void refresh();
    [[nodiscard]] const std::vector<InstalledDatabase>& databases() const { return m_databases; }
    [[nodiscard]] const std::vector<DatabaseJob>& jobs() const { return m_jobs; }

    /// Imports a file (PGN, or an archive of PGN files) as a new database called `name`.
    /// Returns the job's id.
    int importFile(const QString& path, const QString& name, const QString& source = {});
    void cancel(int job);
    /// Moves a database to the Trash. Empty on success, else the error.
    QString remove(const QString& folder);

    /// About how much disk an import of this file needs (bytes), and how much is free.
    [[nodiscard]] static qint64 spaceNeeded(const QString& path);
    [[nodiscard]] qint64 spaceFree() const;

signals:
    void changed();
    void jobChanged(int id);
    /// `error` is empty on success, "cancelled" if it was cancelled.
    void jobFinished(int id, const QString& error);

private:
    struct Running {
        int id = 0;
        std::shared_ptr<std::atomic<bool>> cancel;
        QFuture<void> future;
    };
    void update(int id, const QString& phase, double fraction, quint64 games);
    void finished(int id, const QString& error);

    QString m_root;
    std::vector<InstalledDatabase> m_databases;
    std::vector<DatabaseJob> m_jobs;
    std::vector<Running> m_running;
    int m_nextJob = 1;
    int m_refresh = 0; ///< newer refreshes win
};

} // namespace kiebitz::app
