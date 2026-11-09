#pragma once

#include <QWidget>

namespace chessgui::app {

/// Placeholder board for M0: draws an empty, square 8x8 board that scales with the
/// widget. M2 replaces it with the real board (pieces, move input, arrows, themes).
class BoardView : public QWidget {
    Q_OBJECT

public:
    explicit BoardView(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace chessgui::app
