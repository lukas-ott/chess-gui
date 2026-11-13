#include "ActivityChart.hpp"

#include "style/AppStyle.hpp"

#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace kiebitz::app {
namespace {

constexpr double kLeft = 44;   ///< room for the scale's numbers
constexpr double kBottom = 22; ///< room for the years

/// A round number at or above `value`: 1, 2, 5, 10, 20, 50, ... times a power of ten.
std::uint64_t roundUp(std::uint64_t value) {
    std::uint64_t step = 1;
    while (true) {
        for (const std::uint64_t factor : {1U, 2U, 5U}) {
            if (step * factor >= value) {
                return step * factor;
            }
        }
        step *= 10;
    }
}

} // namespace

ActivityChart::ActivityChart(const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_style(style) {
    setObjectName(QStringLiteral("activityChart"));
    setMinimumHeight(160);
    setMouseTracking(true);
}

void ActivityChart::setYears(const std::map<int, gamedb::Record>& years) {
    m_bars.clear();
    std::uint64_t biggest = 0;
    for (const auto& [year, record] : years) {
        if (year <= 0) {
            continue;
        }
        const Bar bar{year, record.asWhite[0] + record.asBlack[0],
                      record.asWhite[1] + record.asBlack[1], record.asWhite[2] + record.asBlack[2]};
        biggest = std::max(biggest, bar.games());
        m_bars.push_back(bar);
    }
    // Years without games in between show as gaps, so the time axis is honest.
    if (!m_bars.empty()) {
        std::vector<Bar> filled;
        for (int year = m_bars.front().year; year <= m_bars.back().year; ++year) {
            const auto found = std::find_if(m_bars.begin(), m_bars.end(),
                                            [year](const Bar& bar) { return bar.year == year; });
            filled.push_back(found != m_bars.end() ? *found : Bar{year, 0, 0, 0});
        }
        m_bars = std::move(filled);
    }
    m_top = 4 * roundUp((std::max<std::uint64_t>(biggest, 4) + 3) / 4); // four even steps
    update();
}

QRectF ActivityChart::plot() const {
    return QRectF(rect()).adjusted(kLeft, 8, -4, -kBottom);
}

QRectF ActivityChart::barRect(std::size_t index, std::uint64_t top) const {
    const QRectF area = plot();
    const double slot = area.width() / static_cast<double>(std::max<std::size_t>(m_bars.size(), 1));
    const double width = std::min(slot * 0.7, 56.0);
    const double x = area.left() + (slot * static_cast<double>(index)) + ((slot - width) / 2);
    const double height = area.height() * static_cast<double>(top) / static_cast<double>(m_top);
    return {x, area.bottom() - height, width, height};
}

void ActivityChart::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto& c = m_style.colors();
    const QRectF area = plot();
    QFont font = painter.font();
    font.setPixelSize(10);
    painter.setFont(font);
    const QLocale locale;
    // The grid: four steps of the scale.
    for (int k = 0; k <= 4; ++k) {
        const double y = area.bottom() - (area.height() * k / 4.0);
        painter.setPen(QPen(c.border, 1));
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        painter.setPen(c.muted);
        painter.drawText(QRectF(0, y - 7, kLeft - 8, 14), Qt::AlignRight | Qt::AlignVCenter,
                         locale.toString(static_cast<qulonglong>(m_top * k / 4)));
    }
    if (m_bars.empty()) {
        painter.setPen(c.muted);
        painter.drawText(area, Qt::AlignCenter, tr("No dated games yet"));
        return;
    }
    // The bars: won at the bottom, then drawn, then lost; rounded at the top.
    const std::size_t every = std::max<std::size_t>(1, m_bars.size() / 10); // year labels
    for (std::size_t i = 0; i < m_bars.size(); ++i) {
        const Bar& bar = m_bars[i];
        const QRectF whole = barRect(i, bar.games());
        if (bar.games() > 0) {
            QPainterPath shape;
            shape.addRoundedRect(whole, 3, 3);
            painter.save();
            painter.setClipPath(shape);
            std::uint64_t below = 0;
            for (const auto& [count, color] :
                 {std::pair{bar.won, c.good}, std::pair{bar.drawn, c.draws},
                  std::pair{bar.lost, c.bad}}) {
                const QRectF part = barRect(i, below + count);
                painter.fillRect(QRectF(part.left(), part.top(), part.width(),
                                        part.height() - barRect(i, below).height()),
                                 color);
                below += count;
            }
            // A thin gap of the card's colour between the parts, so they don't run together.
            below = 0;
            for (const std::uint64_t count : {bar.won, bar.drawn}) {
                below += count;
                if (count > 0 && below < bar.games()) {
                    const double y = barRect(i, below).top();
                    painter.fillRect(QRectF(whole.left(), y - 1, whole.width(), 2), c.surface);
                }
            }
            painter.restore();
        }
        if (i % every == 0 || i + 1 == m_bars.size()) {
            painter.setPen(c.muted);
            painter.drawText(QRectF(whole.center().x() - 24, area.bottom() + 4, 48, kBottom - 4),
                             Qt::AlignHCenter | Qt::AlignTop, QString::number(bar.year));
        }
    }
}

void ActivityChart::mouseMoveEvent(QMouseEvent* event) {
    for (std::size_t i = 0; i < m_bars.size(); ++i) {
        const QRectF column = barRect(i, m_top);
        if (event->position().x() >= column.left() && event->position().x() <= column.right()) {
            const Bar& bar = m_bars[i];
            const QLocale locale;
            QToolTip::showText(event->globalPosition().toPoint(),
                               tr("%1: %2 games\n%3 won  ·  %4 drawn  ·  %5 lost")
                                   .arg(bar.year)
                                   .arg(locale.toString(static_cast<qulonglong>(bar.games())),
                                        locale.toString(static_cast<qulonglong>(bar.won)),
                                        locale.toString(static_cast<qulonglong>(bar.drawn)),
                                        locale.toString(static_cast<qulonglong>(bar.lost))),
                               this);
            return;
        }
    }
    QToolTip::hideText();
}

} // namespace kiebitz::app
