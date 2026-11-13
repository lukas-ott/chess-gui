#include "ResultBar.hpp"

#include "style/AppStyle.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace kiebitz::app {
namespace {

/// WCAG relative luminance of a colour.
double luminance(const QColor& colour) {
    const auto linear = [](int channel) {
        const double v = channel / 255.0;
        return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return (0.2126 * linear(colour.red())) + (0.7152 * linear(colour.green())) +
           (0.0722 * linear(colour.blue()));
}

/// WCAG contrast ratio of two colours (1 to 21).
double contrast(const QColor& a, const QColor& b) {
    const double x = luminance(a);
    const double y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

} // namespace

ResultBar::ResultBar(const AppStyle& style, int height, Kind kind, QWidget* parent)
    : QWidget(parent), m_style(style), m_kind(kind) {
    setMinimumWidth(110);
    setFixedHeight(height);
}

void ResultBar::setResults(std::uint64_t white, std::uint64_t draws, std::uint64_t black) {
    m_counts = {white, draws, black};
    update();
}

void ResultBar::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const std::uint64_t total = m_counts[0] + m_counts[1] + m_counts[2];
    const QRectF area = QRectF(rect()).adjusted(0, 1, 0, -1);
    QPainterPath clip;
    clip.addRoundedRect(area, area.height() / 2.5, area.height() / 2.5);
    painter.setClipPath(clip);
    const auto& c = m_style.colors();
    if (total == 0) {
        painter.fillRect(area, c.raised);
        return;
    }
    const bool record = m_kind == Kind::Record;
    const std::array<QColor, 3> colors =
        record ? std::array<QColor, 3>{c.good, c.draws, c.bad}
               : std::array<QColor, 3>{c.whiteWins, c.draws, c.blackWins};
    // The percentage in dark or light ink, whichever contrasts more with its part.
    const auto inkOn = [&c](const QColor& fill) {
        return contrast(fill, c.blackWins) >= contrast(fill, c.whiteWins) ? c.blackWins
                                                                          : c.whiteWins;
    };
    QFont font = painter.font();
    font.setPixelSize(height() <= 14 ? 9 : 10);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    double x = area.left();
    for (std::size_t k = 0; k < 3; ++k) {
        const double share = static_cast<double>(m_counts[k]) / static_cast<double>(total);
        const QRectF part(x, area.top(), share * area.width(), area.height());
        painter.fillRect(part, colors[k]);
        const QString text = QStringLiteral("%1%").arg(qRound(share * 100));
        if (part.width() > painter.fontMetrics().horizontalAdvance(text) + 6) {
            painter.setPen(inkOn(colors[k]));
            painter.drawText(part, Qt::AlignCenter, text);
        }
        x += part.width();
    }
    // A fine outline, so the dark (Black) part stays visible on a dark card.
    painter.setClipping(false);
    painter.setPen(QPen(c.border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(area.adjusted(0.5, 0.5, -0.5, -0.5), area.height() / 2.5,
                            area.height() / 2.5);
}

} // namespace kiebitz::app
