#include "Icons.hpp"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace kiebitz::app {
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
    case IconKind::Close:
        painter.drawLine(QPointF(4.5, 4.5), QPointF(11.5, 11.5));
        painter.drawLine(QPointF(11.5, 4.5), QPointF(4.5, 11.5));
        break;
    case IconKind::Folder: { // a folder outline with a tab
        painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath path;
        path.moveTo(2.0, 4.5);
        path.lineTo(6.0, 4.5);
        path.lineTo(7.5, 6.0);
        path.lineTo(14.0, 6.0);
        path.lineTo(14.0, 12.5);
        path.lineTo(2.0, 12.5);
        path.closeSubpath();
        painter.drawPath(path);
        break;
    }
    case IconKind::Board: { // a pawn: head, neck, base
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawEllipse(QPointF(8.0, 4.2), 2.0, 2.0);
        QPainterPath body;
        body.moveTo(6.6, 6.6);
        body.lineTo(9.4, 6.6);
        body.cubicTo(9.4, 9.0, 10.6, 10.6, 11.2, 11.6);
        body.lineTo(4.8, 11.6);
        body.cubicTo(5.4, 10.6, 6.6, 9.0, 6.6, 6.6);
        painter.drawPath(body);
        painter.drawRoundedRect(QRectF(3.8, 11.6, 8.4, 2.2), 0.8, 0.8);
        break;
    }
    case IconKind::Library: { // two pages, one behind the other
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawRoundedRect(QRectF(5.5, 2.0, 7.5, 9.5), 1.5, 1.5);
        QPainterPath back;
        back.moveTo(5.5, 4.5);
        back.lineTo(4.5, 4.5);
        back.cubicTo(3.6, 4.5, 3.0, 5.1, 3.0, 6.0);
        back.lineTo(3.0, 12.5);
        back.cubicTo(3.0, 13.4, 3.6, 14.0, 4.5, 14.0);
        back.lineTo(9.0, 14.0);
        back.cubicTo(9.9, 14.0, 10.5, 13.4, 10.5, 12.5);
        back.lineTo(10.5, 11.5);
        painter.drawPath(back);
        break;
    }
    case IconKind::Engine: { // a chip: a square with pins and a core
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawRoundedRect(QRectF(4.0, 4.0, 8.0, 8.0), 1.5, 1.5);
        painter.drawRect(QRectF(6.5, 6.5, 3.0, 3.0));
        for (const qreal at : {6.0, 8.0, 10.0}) {
            painter.drawLine(QPointF(at, 1.8), QPointF(at, 4.0));
            painter.drawLine(QPointF(at, 12.0), QPointF(at, 14.2));
            painter.drawLine(QPointF(1.8, at), QPointF(4.0, at));
            painter.drawLine(QPointF(12.0, at), QPointF(14.2, at));
        }
        break;
    }
    case IconKind::Cloud: {
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath cloud;
        cloud.moveTo(4.5, 12.0);
        cloud.cubicTo(2.0, 12.0, 1.5, 8.5, 4.0, 8.0);
        cloud.cubicTo(4.0, 4.5, 9.0, 3.5, 10.2, 6.6);
        cloud.cubicTo(12.8, 5.8, 14.8, 8.2, 13.8, 10.2);
        cloud.cubicTo(13.4, 11.4, 12.6, 12.0, 11.5, 12.0);
        cloud.closeSubpath();
        painter.drawPath(cloud);
        break;
    }
    case IconKind::Plus:
        painter.drawLine(QPointF(8.0, 3.5), QPointF(8.0, 12.5));
        painter.drawLine(QPointF(3.5, 8.0), QPointF(12.5, 8.0));
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

} // namespace kiebitz::app
