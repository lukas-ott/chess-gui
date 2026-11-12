#include "Toggle.hpp"

#include "style/AppStyle.hpp"

#include <QPainter>

namespace kiebitz::app {

Toggle::Toggle(const AppStyle& style, QWidget* parent) : QAbstractButton(parent), m_style(style) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(36, 20); // as sizeHint() says
}

void Toggle::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto& c = m_style.colors();
    const QRectF track = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(Qt::NoPen);
    painter.setBrush(isChecked() ? c.accent : c.border);
    if (!isEnabled()) {
        painter.setOpacity(0.4);
    }
    painter.drawRoundedRect(track, track.height() / 2, track.height() / 2);
    const qreal knob = track.height() - 4;
    const qreal x = isChecked() ? track.right() - knob - 2 : track.left() + 2;
    painter.setBrush(isChecked() ? c.accentText : c.text);
    painter.drawEllipse(QRectF(x, track.top() + 2, knob, knob));
}

} // namespace kiebitz::app
