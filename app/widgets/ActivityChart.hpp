#pragma once

#include "kiebitz/gamedb/database.hpp"

#include <QWidget>

#include <map>

namespace kiebitz::app {

class AppStyle;

/// Games per year as stacked bars: won (bottom), drawn, lost, with a light grid and the years
/// under the bars. Hovering a bar says its numbers.
class ActivityChart : public QWidget {
    Q_OBJECT

public:
    explicit ActivityChart(const AppStyle& style, QWidget* parent = nullptr);
    /// By year (0, games without a date, is left out).
    void setYears(const std::map<int, gamedb::Record>& years);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    struct Bar {
        int year = 0;
        std::uint64_t won = 0, drawn = 0, lost = 0;
        [[nodiscard]] std::uint64_t games() const { return won + drawn + lost; }
    };
    [[nodiscard]] QRectF plot() const;
    [[nodiscard]] QRectF barRect(std::size_t index, std::uint64_t top) const;

    const AppStyle& m_style;
    std::vector<Bar> m_bars;
    std::uint64_t m_top = 1; ///< the scale's top: a round number at or above the biggest year
};

} // namespace kiebitz::app
