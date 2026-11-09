#include "BoardView.hpp"

#include <QPainter>

#include <algorithm>

namespace chessgui::app {
namespace {

const QColor kLightSquare(0xEE, 0xD8, 0xB5);
const QColor kDarkSquare(0xB5, 0x88, 0x63);
constexpr int kMargin = 12;

} // namespace

BoardView::BoardView(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QSize BoardView::sizeHint() const {
    return {640, 640};
}

QSize BoardView::minimumSizeHint() const {
    return {240, 240};
}

void BoardView::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);

    // Largest square board that fits, snapped to whole pixels per square for crisp edges.
    const int available = std::min(width(), height()) - 2 * kMargin;
    const int squareSize = std::max(available / 8, 1);
    const int boardSize = squareSize * 8;
    const int left = (width() - boardSize) / 2;
    const int top = (height() - boardSize) / 2;

    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const bool light = (rank + file) % 2 == 0;
            painter.fillRect(left + file * squareSize, top + rank * squareSize, squareSize,
                             squareSize, light ? kLightSquare : kDarkSquare);
        }
    }
}

} // namespace chessgui::app
