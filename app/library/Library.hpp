#pragma once

#include "document/GameDocument.hpp"

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <memory>
#include <vector>

namespace kiebitz::app {

struct LibraryCache; // the index cache, opened on first use (on a worker thread)

/// One PGN file in the library folder.
struct LibraryFile {
    QString path;
    QString name; ///< path relative to the library folder, e.g. "Openings/Najdorf.pgn"
    qint64 size = 0;
    QDateTime modified;
    std::shared_ptr<PgnFile> pgn; ///< the game index (without the file's text)
};

/// The library: every PGN file in one folder (and its subfolders), indexed so the game lists
/// are ready to browse. Like En Croissant's documents folder; by default ~/Documents/Kiebitz.
///
/// Scanning runs in the background: list the files, then read and index every new or changed
/// file in parallel (unchanged files keep their index). The folder is watched, so files that
/// are added, changed or removed show up by themselves.
///
/// Indexes are kept in a cache on disk (see kiebitz::library::IndexCache), so on the next start
/// the whole library is listed from it and only files changed since are read. The Library keeps
/// no file's text in memory: a game is read from disk when it's previewed or opened.
class Library : public QObject {
    Q_OBJECT

public:
    explicit Library(QObject* parent = nullptr);
    /// Tells a running scan to stop reading files, so quitting never waits for it.
    ~Library() override;
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    Library(Library&&) = delete;
    Library& operator=(Library&&) = delete;

    [[nodiscard]] static QString defaultFolder();
    /// Where the index cache lives: the app's cache folder, or KIEBITZ_SETTINGS_DIR when set.
    [[nodiscard]] static QString defaultCacheFile();

    /// Keeps indexes in this file (call before setFolder). Without one, every file is read on
    /// every scan.
    void setCacheFile(const QString& path);

    /// Uses this folder (created if missing) and scans it.
    void setFolder(const QString& folder);
    [[nodiscard]] const QString& folder() const noexcept { return m_folder; }
    [[nodiscard]] const std::vector<LibraryFile>& files() const noexcept { return m_files; }
    [[nodiscard]] bool isScanning() const noexcept { return m_scanning; }

    /// Scans again now (normally automatic).
    void rescan();

    /// Files are still being read (their game counts are not known yet).
    [[nodiscard]] bool isReading() const noexcept { return m_reading > 0; }

signals:
    /// files() changed while a scan runs: the file list is known (right away, even for files
    /// iCloud has moved off the Mac), and more of the files have been read since. Files not read
    /// yet have no `pgn`.
    void progress();
    /// A scan finished and files() changed: how many files and games, and how long it took.
    void scanned(int files, int games, qint64 milliseconds);

private:
    void startScan();
    void readFiles(int generation, qint64 listedAfterMs);
    void watch();

    QString m_folder;
    std::vector<LibraryFile> m_files;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce; ///< folders change in bursts (a save is several events): wait a moment
    bool m_scanning = false;
    int m_reading = 0;      ///< files still being read
    QTimer m_progressTimer; ///< progress() goes out at most every 200 ms
    bool m_rescanQueued = false;
    int m_reportedGeneration = -1; ///< the folder (generation) `scanned` was last emitted for
    int m_generation = 0; ///< results of an older scan (folder changed meanwhile) are dropped
    /// Set when the library goes away. Reading a file iCloud has moved off the Mac waits for the
    /// download, and the app can't quit until running scans end, so they check this.
    std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<LibraryCache> m_cache; ///< shared with the scan's worker threads
};

} // namespace kiebitz::app
