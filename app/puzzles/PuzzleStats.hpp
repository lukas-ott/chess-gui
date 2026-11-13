#pragma once

#include "kiebitz/puzzles/history.hpp"

#include <QObject>
#include <QString>
#include <QThreadPool>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kiebitz::app {

/// The puzzle history for the window: puzzles::History on a worker thread, so the database is
/// never touched on the UI thread.
class PuzzleStats : public QObject {
    Q_OBJECT

public:
    /// What a source's history adds up to, read in the background.
    struct Summary {
        puzzles::Totals totals;
        std::vector<puzzles::Attempt> recent; ///< newest first
        std::map<std::string, bool> latest;   ///< each puzzle's latest result (clean or not)
    };

    /// `path`: the database (empty: the default, in the app's data folder; tests set
    /// KIEBITZ_SETTINGS_DIR).
    explicit PuzzleStats(const QString& path = {}, QObject* parent = nullptr);
    ~PuzzleStats() override;
    PuzzleStats(const PuzzleStats&) = delete;
    PuzzleStats& operator=(const PuzzleStats&) = delete;
    PuzzleStats(PuzzleStats&&) = delete;
    PuzzleStats& operator=(PuzzleStats&&) = delete;

    [[nodiscard]] static QString defaultFile();

    void record(const puzzles::Attempt& attempt);
    /// Reads a source's summary; loaded() follows.
    void load(const QString& source);
    void clear(const QString& source);
    /// Waits for pending work (tests, quitting).
    void wait();

signals:
    void loaded(const QString& source, const kiebitz::app::PuzzleStats::Summary& summary);

private:
    struct Shared;
    std::shared_ptr<Shared> m_shared;
    QThreadPool m_worker; ///< one thread: writes and reads in order
};

} // namespace kiebitz::app
