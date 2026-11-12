#include "Icons.hpp"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

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
    case IconKind::File: { // a page with its top-right corner folded
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath page;
        page.moveTo(4.0, 2.5);
        page.lineTo(9.5, 2.5);
        page.lineTo(12.5, 5.5);
        page.lineTo(12.5, 13.5);
        page.lineTo(4.0, 13.5);
        page.closeSubpath();
        painter.drawPath(page);
        QPainterPath fold;
        fold.moveTo(9.5, 2.5);
        fold.lineTo(9.5, 5.5);
        fold.lineTo(12.5, 5.5);
        painter.drawPath(fold);
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
    case IconKind::Branch: { // a stem from the bottom that splits into two lines
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawEllipse(QPointF(8.0, 12.6), 1.5, 1.5);
        painter.drawEllipse(QPointF(4.4, 3.4), 1.5, 1.5);
        painter.drawEllipse(QPointF(11.6, 3.4), 1.5, 1.5);
        QPainterPath lines;
        lines.moveTo(8.0, 11.1);
        lines.lineTo(8.0, 9.0);
        lines.cubicTo(8.0, 7.4, 4.4, 7.0, 4.4, 4.9);
        lines.moveTo(8.0, 9.0);
        lines.cubicTo(8.0, 7.4, 11.6, 7.0, 11.6, 4.9);
        painter.drawPath(lines);
        break;
    }
    case IconKind::Trophy: { // a cup with handles on a stem and foot
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath cup;
        cup.moveTo(4.8, 2.6);
        cup.lineTo(11.2, 2.6);
        cup.lineTo(11.2, 5.4);
        cup.cubicTo(11.2, 7.6, 9.8, 9.0, 8.0, 9.0);
        cup.cubicTo(6.2, 9.0, 4.8, 7.6, 4.8, 5.4);
        cup.closeSubpath();
        cup.moveTo(4.8, 3.8);
        cup.cubicTo(2.4, 3.8, 2.4, 6.8, 5.2, 6.9);
        cup.moveTo(11.2, 3.8);
        cup.cubicTo(13.6, 3.8, 13.6, 6.8, 10.8, 6.9);
        cup.moveTo(8.0, 9.0);
        cup.lineTo(8.0, 11.6);
        cup.moveTo(5.6, 13.2);
        cup.lineTo(10.4, 13.2);
        cup.moveTo(6.6, 11.6);
        cup.lineTo(9.4, 11.6);
        painter.drawPath(cup);
        break;
    }
    case IconKind::Puzzle: { // a square piece with a knob on top and one on the right
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath square;
        square.addRoundedRect(QRectF(2.8, 5.0, 8.4, 8.4), 1.0, 1.0);
        QPainterPath knobs;
        knobs.addEllipse(QPointF(7.0, 4.0), 1.8, 1.8);
        knobs.addEllipse(QPointF(12.2, 9.2), 1.8, 1.8);
        painter.drawPath(square.united(knobs));
        break;
    }
    case IconKind::NewFile: { // the page, a plus beside it
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath page;
        page.moveTo(9.0, 13.5);
        page.lineTo(3.0, 13.5);
        page.lineTo(3.0, 2.5);
        page.lineTo(7.5, 2.5);
        page.lineTo(10.5, 5.5);
        page.lineTo(10.5, 7.5);
        page.moveTo(7.5, 2.5);
        page.lineTo(7.5, 5.5);
        page.lineTo(10.5, 5.5);
        painter.drawPath(page);
        painter.drawLine(QPointF(12.0, 9.5), QPointF(12.0, 14.0));
        painter.drawLine(QPointF(9.75, 11.75), QPointF(14.25, 11.75));
        break;
    }
    case IconKind::NewFolder: { // the folder, a plus in it
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath path;
        path.moveTo(2.0, 4.0);
        path.lineTo(6.0, 4.0);
        path.lineTo(7.5, 5.5);
        path.lineTo(14.0, 5.5);
        path.lineTo(14.0, 12.5);
        path.lineTo(2.0, 12.5);
        path.closeSubpath();
        painter.drawPath(path);
        painter.drawLine(QPointF(8.0, 7.2), QPointF(8.0, 10.8));
        painter.drawLine(QPointF(6.2, 9.0), QPointF(9.8, 9.0));
        break;
    }
    case IconKind::Database: { // a cylinder: top, sides, one band, bottom
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawEllipse(QRectF(3.0, 2.0, 10.0, 3.6));
        QPainterPath body;
        body.moveTo(3.0, 3.8);
        body.lineTo(3.0, 12.2);
        body.arcTo(QRectF(3.0, 10.4, 10.0, 3.6), 180.0, 180.0);
        body.lineTo(13.0, 3.8);
        body.moveTo(3.0, 8.0);
        body.arcTo(QRectF(3.0, 6.2, 10.0, 3.6), 180.0, 180.0);
        painter.drawPath(body);
        break;
    }
    case IconKind::Person: { // a head and shoulders
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawEllipse(QPointF(8.0, 5.2), 2.6, 2.6);
        QPainterPath shoulders;
        shoulders.moveTo(3.0, 13.5);
        shoulders.cubicTo(3.0, 10.2, 5.2, 8.9, 8.0, 8.9);
        shoulders.cubicTo(10.8, 8.9, 13.0, 10.2, 13.0, 13.5);
        painter.drawPath(shoulders);
        break;
    }
    case IconKind::Info: { // an "i" in a circle
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawEllipse(QPointF(8.0, 8.0), 6.0, 6.0);
        painter.drawLine(QPointF(8.0, 7.2), QPointF(8.0, 11.2));
        painter.setPen(QPen(color, 1.9, Qt::SolidLine, Qt::RoundCap));
        painter.drawPoint(QPointF(8.0, 4.9));
        break;
    }
    case IconKind::Calendar: { // a rounded page with two rings and a header line
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawRoundedRect(QRectF(2.5, 3.5, 11.0, 10.0), 2.0, 2.0);
        painter.drawLine(QPointF(2.5, 6.5), QPointF(13.5, 6.5));
        painter.drawLine(QPointF(5.5, 2.0), QPointF(5.5, 4.5));
        painter.drawLine(QPointF(10.5, 2.0), QPointF(10.5, 4.5));
        break;
    }
    case IconKind::Refresh: { // most of a circle, with an arrow head
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawArc(QRectF(3.0, 3.0, 10.0, 10.0), 60 * 16, 290 * 16);
        QPainterPath head;
        head.moveTo(10.6, 2.0);
        head.lineTo(11.0, 4.6);
        head.lineTo(8.4, 5.0);
        painter.drawPath(head);
        break;
    }
    case IconKind::Trash: { // a lid with a handle, and a bin with two lines
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(3.0, 4.5), QPointF(13.0, 4.5));
        painter.drawLine(QPointF(6.5, 4.5), QPointF(6.5, 2.8));
        painter.drawLine(QPointF(6.5, 2.8), QPointF(9.5, 2.8));
        painter.drawLine(QPointF(9.5, 2.8), QPointF(9.5, 4.5));
        QPainterPath bin;
        bin.moveTo(4.3, 4.5);
        bin.lineTo(5.0, 13.2);
        bin.lineTo(11.0, 13.2);
        bin.lineTo(11.7, 4.5);
        painter.drawPath(bin);
        painter.drawLine(QPointF(7.0, 7.0), QPointF(7.0, 11.0));
        painter.drawLine(QPointF(9.0, 7.0), QPointF(9.0, 11.0));
        break;
    }
    case IconKind::Check:
        painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(
            QPolygonF({QPointF(3.5, 8.5), QPointF(6.6, 11.5), QPointF(12.5, 4.5)}));
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
