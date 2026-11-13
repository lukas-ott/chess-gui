#include "TrainingHistory.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

#include <mutex>

namespace kiebitz::app {
namespace {

constexpr qint64 kUnixEpochJulianDay = 2440588; ///< 1 January 1970

} // namespace

struct TrainingHistory::Shared {
    std::string path;
    std::once_flag opened;
    std::unique_ptr<training::Log> log;

    /// The database, or null if it can't be opened (training works without it).
    training::Log* get() {
        std::call_once(opened, [this] {
            QDir().mkpath(QFileInfo(QString::fromStdString(path)).path());
            if (auto result = training::Log::open(path); result.ok()) {
                log = std::move(*result);
            }
        });
        return log.get();
    }
};

TrainingHistory::TrainingHistory(const QString& path, QObject* parent)
    : QObject(parent), m_shared(std::make_shared<Shared>()) {
    m_shared->path = (path.isEmpty() ? defaultFile() : path).toStdString();
    m_worker.setMaxThreadCount(1);
}

TrainingHistory::~TrainingHistory() {
    m_worker.waitForDone();
}

QString TrainingHistory::defaultFile() {
    QString folder = qEnvironmentVariable("KIEBITZ_SETTINGS_DIR"); // tests: never the real one
    if (folder.isEmpty()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    return QDir(folder).filePath(QStringLiteral("training.sqlite"));
}

training::Day TrainingHistory::dayOf(const QDate& date) {
    return static_cast<training::Day>(date.toJulianDay() - kUnixEpochJulianDay);
}

QDate TrainingHistory::dateOf(training::Day day) {
    return QDate::fromJulianDay(day + kUnixEpochJulianDay);
}

void TrainingHistory::add(training::Part part, int seconds) {
    const training::Day today = dayOf(QDate::currentDate());
    (void)QtConcurrent::run(&m_worker, [shared = m_shared, today, part, seconds] {
        if (auto* log = shared->get()) {
            log->add(today, part, seconds);
        }
    });
    load();
}

void TrainingHistory::load() {
    const training::Day today = dayOf(QDate::currentDate());
    // Owned by this object and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher =
        new QFutureWatcher<Summary>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<Summary>::finished, this, [this, watcher] {
        watcher->deleteLater();
        emit loaded(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run(&m_worker, [shared = m_shared, today] {
        Summary summary;
        if (const auto* log = shared->get()) {
            summary.days = log->days(0); // a day a row: small even after years
            const std::set<training::Day> trained = log->trainedDays();
            summary.streak = training::streak(trained, today);
            summary.bestStreak = training::bestStreak(trained);
        }
        return summary;
    }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

void TrainingHistory::wait() {
    m_worker.waitForDone();
}

} // namespace kiebitz::app
