#include "BoardGeometry.hpp"

#include <algorithm>

namespace kiebitz::app {

BoardGeometry::BoardGeometry(QSize widgetSize, int margin, bool flipped) : m_flipped(flipped) {
    const int available = std::min(widgetSize.width(), widgetSize.height()) - (2 * margin);
    m_squareSize = std::max(available / 8, 1);
    const int boardSize = m_squareSize * 8;
    m_board = QRect((widgetSize.width() - boardSize) / 2, (widgetSize.height() - boardSize) / 2,
                    boardSize, boardSize);
}

int BoardGeometry::column(core::Square square) const noexcept {
    return m_flipped ? 7 - square.file() : square.file();
}

int BoardGeometry::row(core::Square square) const noexcept {
    return m_flipped ? square.rank() : 7 - square.rank();
}

QRect BoardGeometry::squareRect(core::Square square) const noexcept {
    return {m_board.left() + (column(square) * m_squareSize),
            m_board.top() + (row(square) * m_squareSize), m_squareSize, m_squareSize};
}

QPointF BoardGeometry::squareCenter(core::Square square) const noexcept {
    return QRectF(squareRect(square)).center();
}

std::optional<core::Square> BoardGeometry::squareAt(QPoint point) const noexcept {
    if (!m_board.contains(point) || m_squareSize <= 0) {
        return std::nullopt;
    }
    const int col = (point.x() - m_board.left()) / m_squareSize;
    const int row = (point.y() - m_board.top()) / m_squareSize;
    const int file = m_flipped ? 7 - col : col;
    const int rank = m_flipped ? row : 7 - row;
    return core::Square::at(file, rank);
}

} // namespace kiebitz::app
