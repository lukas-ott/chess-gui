#include "PuzzleStats.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

#include <mutex>

namespace kiebitz::app {

struct PuzzleStats::Shared {
    std::string path;
    std::once_flag opened;
    std::unique_ptr<puzzles::History> history;

    /// The database, or null if it can't be opened (puzzles work without it).
    puzzles::History* get() {
        std::call_once(opened, [this] {
            QDir().mkpath(QFileInfo(QString::fromStdString(path)).path());
            if (auto result = puzzles::History::open(path); result.ok()) {
                history = std::move(*result);
            }
        });
        return history.get();
    }
};

PuzzleStats::PuzzleStats(const QString& path, QObject* parent)
    : QObject(parent), m_shared(std::make_shared<Shared>()) {
    m_shared->path = (path.isEmpty() ? defaultFile() : path).toStdString();
    m_worker.setMaxThreadCount(1);
}

PuzzleStats::~PuzzleStats() {
    m_worker.waitForDone();
}

QString PuzzleStats::defaultFile() {
    QString folder = qEnvironmentVariable("KIEBITZ_SETTINGS_DIR"); // tests: never the real one
    if (folder.isEmpty()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    return QDir(folder).filePath(QStringLiteral("puzzles.sqlite"));
}

void PuzzleStats::record(const puzzles::Attempt& attempt) {
    (void)QtConcurrent::run(
        &m_worker, [shared = m_shared, copy = std::make_shared<const puzzles::Attempt>(attempt)] {
            if (auto* history = shared->get()) {
                history->record(*copy);
            }
        });
}

void PuzzleStats::load(const QString& source) {
    // Owned by this object and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher =
        new QFutureWatcher<Summary>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<Summary>::finished, this, [this, watcher, source] {
        watcher->deleteLater();
        emit loaded(source, watcher->result());
    });
    watcher->setFuture(
        QtConcurrent::run(&m_worker, [shared = m_shared, key = source.toStdString()] {
            Summary summary;
            if (const auto* history = shared->get()) {
                summary.totals = history->totals(key);
                summary.recent = history->recent(key, 50);
                summary.latest = history->latest(key);
            }
            return summary;
        }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

void PuzzleStats::clear(const QString& source) {
    (void)QtConcurrent::run(&m_worker, [shared = m_shared, key = source.toStdString()] {
        if (auto* history = shared->get()) {
            history->clear(key);
        }
    });
}

void PuzzleStats::wait() {
    m_worker.waitForDone();
}

} // namespace kiebitz::app
