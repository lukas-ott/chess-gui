#pragma once

#include "kiebitz/training/log.hpp"

#include <QDate>
#include <QObject>
#include <QString>
#include <QThreadPool>

#include <map>
#include <memory>

namespace kiebitz::app {

/// The training log for the window: training::Log on a worker thread, so the database is
/// never touched on the UI thread.
class TrainingHistory : public QObject {
    Q_OBJECT

public:
    /// What the log adds up to, read in the background.
    struct Summary {
        std::map<training::Day, training::DaySeconds> days; ///< every day trained
        int streak = 0;
        int bestStreak = 0;
    };

    /// `path`: the database (empty: the default, in the app's data folder; tests set
    /// KIEBITZ_SETTINGS_DIR).
    explicit TrainingHistory(const QString& path = {}, QObject* parent = nullptr);
    ~TrainingHistory() override;
    TrainingHistory(const TrainingHistory&) = delete;
    TrainingHistory& operator=(const TrainingHistory&) = delete;
    TrainingHistory(TrainingHistory&&) = delete;
    TrainingHistory& operator=(TrainingHistory&&) = delete;

    [[nodiscard]] static QString defaultFile();
    /// A date as the log counts days, and back.
    [[nodiscard]] static training::Day dayOf(const QDate& date);
    [[nodiscard]] static QDate dateOf(training::Day day);

    /// Adds time to today's part; loaded() follows with the new totals.
    void add(training::Part part, int seconds);
    /// Reads the summary; loaded() follows.
    void load();
    /// Waits for pending work (tests, quitting).
    void wait();

signals:
    void loaded(const kiebitz::app::TrainingHistory::Summary& summary);

private:
    struct Shared;
    std::shared_ptr<Shared> m_shared;
    QThreadPool m_worker; ///< one thread: writes and reads in order
};

} // namespace kiebitz::app
