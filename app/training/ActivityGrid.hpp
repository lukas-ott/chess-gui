#pragma once

#include "kiebitz/training/log.hpp"

#include <QWidget>

#include <map>
#include <optional>

namespace kiebitz::app {

class AppStyle;

/// The days trained, as on GitHub: a square per day, a column per week, shaded by the minutes
/// trained: the last twelve months, or one calendar year. Hovering a day says its minutes, by
/// part. The squares grow with the width (a year always fits); every view is the same size.
class ActivityGrid : public QWidget {
    Q_OBJECT

public:
    explicit ActivityGrid(const AppStyle& style, QWidget* parent = nullptr);
    void setDays(std::map<training::Day, training::DaySeconds> days, training::Day today);
    /// One calendar year; none: the last twelve months.
    void setYear(std::optional<int> year);
    [[nodiscard]] std::optional<int> year() const noexcept { return m_year; }

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int width) const override;
    /// The weeks (columns) shown.
    [[nodiscard]] int weeks() const;
    /// A day's square, or empty if it isn't shown (tests).
    [[nodiscard]] QRect cellOf(training::Day day) const;
    /// How dark a day's square is: 0 (nothing) to 4 (an hour or more).
    [[nodiscard]] int levelOf(training::Day day) const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    /// The row of a day: 0 for the locale's first day of the week.
    [[nodiscard]] static int rowOf(training::Day day);
    /// The first and last day shown.
    [[nodiscard]] training::Day firstDay() const;
    [[nodiscard]] training::Day lastDay() const;
    /// The first day of the leftmost column (it may be before firstDay()).
    [[nodiscard]] training::Day firstColumnDay() const;
    /// A square's side, for a width.
    [[nodiscard]] static int cellFor(int width);
    [[nodiscard]] int cell() const { return cellFor(width()); }
    [[nodiscard]] QColor shade(int level) const;

    const AppStyle& m_style;
    std::map<training::Day, training::DaySeconds> m_days;
    training::Day m_today = 0;
    std::optional<int> m_year;
};

} // namespace kiebitz::app
