#include "PracticeStats.hpp"

#include "kiebitz/library/training_stats.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

#include <mutex>

namespace kiebitz::app {

struct PracticeStats::Shared {
    std::string path;
    std::once_flag opened;
    std::unique_ptr<library::TrainingStats> stats;

    /// The database, or null if it can't be opened (practice works without it).
    library::TrainingStats* get() {
        std::call_once(opened, [this] {
            QDir().mkpath(QFileInfo(QString::fromStdString(path)).path());
            if (auto result = library::TrainingStats::open(path); result.ok()) {
                stats = std::move(*result);
            }
        });
        return stats.get();
    }
};

PracticeStats::PracticeStats(const QString& path, QObject* parent)
    : QObject(parent), m_shared(std::make_shared<Shared>()) {
    m_shared->path = (path.isEmpty() ? defaultFile() : path).toStdString();
    m_worker.setMaxThreadCount(1);
}

PracticeStats::~PracticeStats() {
    m_worker.waitForDone();
}

QString PracticeStats::defaultFile() {
    // Your own data, not a cache: in Application Support, where macOS never clears it.
    QString folder = qEnvironmentVariable("KIEBITZ_SETTINGS_DIR"); // tests: never the real one
    if (folder.isEmpty()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    return QDir(folder).filePath(QStringLiteral("practice.sqlite"));
}

void PracticeStats::record(core::Color side, std::uint64_t position, bool right) {
    (void)QtConcurrent::run(&m_worker, [shared = m_shared, side, position, right] {
        if (auto* stats = shared->get()) {
            stats->record(side, position, right);
        }
    });
}

void PracticeStats::load(core::Color side) {
    using Set = std::unordered_set<std::uint64_t>;
    // Owned by this object and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher =
        new QFutureWatcher<Set>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<Set>::finished, this, [this, watcher, side] {
        watcher->deleteLater();
        m_mistakes[side == core::Color::White ? 0 : 1] = watcher->result();
        emit mistakesChanged(side);
    });
    watcher->setFuture(QtConcurrent::run(&m_worker, [shared = m_shared, side] {
        const auto* stats = shared->get();
        return stats != nullptr ? stats->mistakes(side) : Set{};
    }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

const std::unordered_set<std::uint64_t>* PracticeStats::mistakes(core::Color side) const {
    const auto& found = m_mistakes[side == core::Color::White ? 0 : 1];
    return found ? &*found : nullptr;
}

void PracticeStats::wait() {
    m_worker.waitForDone();
}

} // namespace kiebitz::app
