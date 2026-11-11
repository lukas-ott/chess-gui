#pragma once

#include "kiebitz/core/types.hpp"

#include <QObject>
#include <QThreadPool>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>

namespace kiebitz::library {
class TrainingStats;
}

namespace kiebitz::app {

/// The practice statistics for the window: library::TrainingStats on a worker thread, so the
/// database is never touched on the UI thread. Your answers are recorded as you practise;
/// the positions you still get wrong are kept here per side once loaded.
class PracticeStats : public QObject {
    Q_OBJECT

public:
    /// `path`: the database (empty: the default, in the app's data folder; tests set
    /// KIEBITZ_SETTINGS_DIR).
    explicit PracticeStats(const QString& path = {}, QObject* parent = nullptr);
    ~PracticeStats() override;
    PracticeStats(const PracticeStats&) = delete;
    PracticeStats& operator=(const PracticeStats&) = delete;
    PracticeStats(PracticeStats&&) = delete;
    PracticeStats& operator=(PracticeStats&&) = delete;

    [[nodiscard]] static QString defaultFile();

    /// Your first answer at a position.
    void record(core::Color side, std::uint64_t position, bool right);
    /// Reads the positions you still get wrong (mistakesChanged() follows).
    void load(core::Color side);
    /// What load() found, or null before.
    [[nodiscard]] const std::unordered_set<std::uint64_t>* mistakes(core::Color side) const;

    /// Waits for pending writes (tests, quitting).
    void wait();

signals:
    void mistakesChanged(kiebitz::core::Color side);

private:
    struct Shared;
    std::shared_ptr<Shared> m_shared; ///< the database, opened on first use on the worker
    QThreadPool m_worker;             ///< one thread: writes and reads in order
    std::array<std::optional<std::unordered_set<std::uint64_t>>, 2> m_mistakes;
};

} // namespace kiebitz::app
