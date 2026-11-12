#include "ResultBar.hpp"

#include "style/AppStyle.hpp"

#include <QPainter>
#include <QPainterPath>

namespace kiebitz::app {

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
    const std::array<QColor, 3> texts =
        record ? std::array<QColor, 3>{c.blackWins, c.whiteWins, c.whiteWins}
               : std::array<QColor, 3>{c.blackWins, c.whiteWins, c.whiteWins};
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
            painter.setPen(texts[k]);
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
