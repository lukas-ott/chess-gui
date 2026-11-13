#pragma once

#include "TrainingHistory.hpp"
#include "kiebitz/training/session.hpp"

#include <QWidget>

#include <array>

class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;

namespace kiebitz::app {

class ActivityGrid;
class AppStyle;
class PageHeader;
class Stepper;
class Toggle;
class TrainingSession;

/// The Training page: the plan (minutes per part, which parts), Start, and what you have
/// trained: today, the streak, and a GitHub-like grid of the days.
class TrainingView : public QWidget {
    Q_OBJECT

public:
    TrainingView(TrainingSession& session, TrainingHistory& stats, const AppStyle& style,
                 QWidget* parent = nullptr);

    /// The page is shown: the numbers read again.
    void activate();
    // For tests
    [[nodiscard]] ActivityGrid* grid() const { return m_grid; }

private:
    void savePlan();
    void showSession();
    void showStats(const TrainingHistory::Summary& summary);

    TrainingSession& m_session;
    TrainingHistory& m_stats;
    const AppStyle& m_style;
    PageHeader* m_header = nullptr;
    QPushButton* m_start = nullptr;
    struct Row {
        QWidget* frame = nullptr;
        Toggle* on = nullptr;
        Stepper* minutes = nullptr;
        QLabel* left = nullptr; ///< "12:34 left" while it's the part running
    };
    std::array<Row, training::kParts> m_rows{};
    QComboBox* m_side = nullptr; ///< the openings part: which repertoire
    QLabel* m_total = nullptr;
    std::array<QLabel*, 4> m_tiles{}; ///< today, streak, best streak, the last 7 days
    ActivityGrid* m_grid = nullptr;
    QComboBox* m_year = nullptr; ///< the last 12 months, or a year (once there are several)
    TrainingHistory::Summary m_summary;
};

/// The running session in the status bar, on every page: the part, its time, and Pause, Next
/// and Stop.
class TrainingBar : public QWidget {
    Q_OBJECT

public:
    TrainingBar(TrainingSession& session, const AppStyle& style, QWidget* parent = nullptr);
    [[nodiscard]] QString text() const; ///< tests

signals:
    /// The part's name was clicked: show the Training page.
    void showRequested();

private:
    void refresh();

    TrainingSession& m_session;
    const AppStyle& m_style;
    QToolButton* m_part = nullptr;
    QToolButton* m_pause = nullptr;
    QToolButton* m_next = nullptr;
    QToolButton* m_stop = nullptr;
};

/// "12:34", "1:02:03".
[[nodiscard]] QString clockText(qint64 ms);

} // namespace kiebitz::app
