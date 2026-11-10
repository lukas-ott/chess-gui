#pragma once

#include "chessgui/core/types.hpp"

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>

#include <optional>

namespace chessgui::app {

/// Where the board and each square sit inside the widget.
///
/// The board is the largest square that fits, centred, with every square a whole number of
/// pixels so edges stay crisp. When flipped, Black is at the bottom.
class BoardGeometry {
public:
    BoardGeometry() = default;
    BoardGeometry(QSize widgetSize, int margin, bool flipped);

    [[nodiscard]] int squareSize() const noexcept { return m_squareSize; }
    [[nodiscard]] QRect boardRect() const noexcept { return m_board; }
    [[nodiscard]] bool flipped() const noexcept { return m_flipped; }

    [[nodiscard]] QRect squareRect(core::Square square) const noexcept;
    [[nodiscard]] QPointF squareCenter(core::Square square) const noexcept;
    /// The square under a point, or nothing outside the board.
    [[nodiscard]] std::optional<core::Square> squareAt(QPoint point) const noexcept;

    /// Screen column (0 = left) and row (0 = top) of a square.
    [[nodiscard]] int column(core::Square square) const noexcept;
    [[nodiscard]] int row(core::Square square) const noexcept;

private:
    QRect m_board;
    int m_squareSize = 0;
    bool m_flipped = false;
};

} // namespace chessgui::app
