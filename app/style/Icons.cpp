#include "Icons.hpp"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace chessgui::app {
namespace {

/// Draws the icon on a 16×16 grid, scaled to `size`.
void draw(QPainter& painter, IconKind kind, const QColor& color, int size) {
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 16.0, size / 16.0);
    const QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);

    // A chevron pointing left (or right when mirrored), centred at x.
    const auto chevron = [&](qreal x, bool right) {
        const qreal dx = right ? -2.5 : 2.5;
        QPainterPath path;
        path.moveTo(x + dx, 3.5);
        path.lineTo(x - dx, 8.0);
        path.lineTo(x + dx, 12.5);
        painter.drawPath(path);
    };
    switch (kind) {
    case IconKind::First:
        painter.drawLine(QPointF(4.0, 3.5), QPointF(4.0, 12.5));
        chevron(9.5, false);
        break;
    case IconKind::Previous:
        chevron(8.0, false);
        break;
    case IconKind::Next:
        chevron(8.0, true);
        break;
    case IconKind::Last:
        painter.drawLine(QPointF(12.0, 3.5), QPointF(12.0, 12.5));
        chevron(6.5, true);
        break;
    }
}

QPixmap render(IconKind kind, const QColor& color, int size) {
    const qreal ratio = qGuiApp != nullptr ? qGuiApp->devicePixelRatio() : 1.0;
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    draw(painter, kind, color, size);
    return pixmap;
}

} // namespace

QIcon makeIcon(IconKind kind, const QColor& color, const QColor& disabledColor, int size) {
    QIcon icon;
    icon.addPixmap(render(kind, color, size), QIcon::Normal);
    icon.addPixmap(render(kind, disabledColor, size), QIcon::Disabled);
    return icon;
}

} // namespace chessgui::app
