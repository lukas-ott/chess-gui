#pragma once

#include "BoardGeometry.hpp"
#include "BoardTheme.hpp"
#include "PieceSet.hpp"
#include "chessgui/core/annotations.hpp"
#include "chessgui/core/position.hpp"

#include <QPoint>
#include <QWidget>

#include <optional>
#include <vector>

class QVariantAnimation;

namespace chessgui::app {

/// The interactive chess board.
///
/// It shows a position and lets the user play legal moves (click-click or drag-and-drop, with a
/// promotion picker) and draw arrows and square marks with the right mouse button. It never
/// changes the game itself: it reports moves and drawings through signals, and the owner decides
/// what to do with them and calls setPosition() / setDrawings() back.
///
/// Drawing colours follow Lichess: plain = green, Shift = red, Option = blue,
/// Shift+Option = yellow. A left click on the board clears the drawings.
class BoardView : public QWidget {
    Q_OBJECT

public:
    explicit BoardView(QWidget* parent = nullptr);
    ~BoardView() override;
    BoardView(const BoardView&) = delete;
    BoardView& operator=(const BoardView&) = delete;
    BoardView(BoardView&&) = delete;
    BoardView& operator=(BoardView&&) = delete;

    /// Shows a position. `lastMove` is highlighted; with `animate`, it slides into place.
    void setPosition(const core::Position& position, std::optional<core::Move> lastMove = {},
                     bool animate = false);
    void setDrawings(std::vector<core::Arrow> arrows, std::vector<core::SquareMark> marks);
    /// The continuations to show as thin arrows at a branch point, and which one is chosen
    /// (-1 for none). Pass an empty list to hide them.
    void setCandidates(std::vector<core::Move> moves, int chosen);
    [[nodiscard]] const core::Position& position() const noexcept { return m_position; }
    [[nodiscard]] const std::vector<core::Arrow>& arrows() const noexcept { return m_arrows; }
    [[nodiscard]] const std::vector<core::SquareMark>& squareMarks() const noexcept {
        return m_marks;
    }
    /// Where a square is drawn (for tests and overlays).
    [[nodiscard]] QRect squareRect(core::Square square) const noexcept {
        return m_geometry.squareRect(square);
    }

    void setFlipped(bool flipped);
    [[nodiscard]] bool isFlipped() const noexcept { return m_flipped; }
    void setShowCoordinates(bool show);
    [[nodiscard]] bool showCoordinates() const noexcept { return m_showCoordinates; }
    void setTheme(const BoardTheme& theme);
    [[nodiscard]] const BoardTheme& theme() const noexcept { return m_theme; }
    void setPieceSet(const QString& name);
    /// A read-only board (e.g. a preview) ignores the mouse: no moves, no drawings.
    void setInteractive(bool interactive) { m_interactive = interactive; }
    [[nodiscard]] QString pieceSetName() const { return m_pieces.name(); }

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int width) const override { return width; }

signals:
    /// The user played a legal move in the shown position.
    void moveMade(chessgui::core::Move move);
    /// The user drew or removed an arrow or square mark.
    void drawingsChanged(const std::vector<chessgui::core::Arrow>& arrows,
                         const std::vector<chessgui::core::SquareMark>& marks);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Promotion {
        core::Square from;
        core::Square to;
    };
    struct Slide {
        core::Square from;
        core::Square to;
    };

    void recomputeGeometry();
    void select(std::optional<core::Square> square);
    /// Plays from→to if legal, asking for the promotion piece first when needed.
    /// Returns false if the move isn't legal.
    bool tryMove(core::Square from, core::Square to);
    [[nodiscard]] bool canPick(core::Square square) const;
    [[nodiscard]] std::vector<core::Move> movesFrom(core::Square square) const;
    [[nodiscard]] static core::MarkColor colorForModifiers(Qt::KeyboardModifiers modifiers);
    [[nodiscard]] std::optional<core::PieceType> promotionChoiceAt(QPoint point) const;
    [[nodiscard]] QRect promotionTile(std::size_t index) const;
    void updateCursor(QPoint point);

    void paintSquares(QPainter& painter) const;
    void paintHighlights(QPainter& painter) const;
    void paintCoordinates(QPainter& painter) const;
    void paintPieces(QPainter& painter);
    void paintMoveHints(QPainter& painter) const;
    void paintArrow(QPainter& painter, core::Square from, core::Square to, const QColor& color,
                    qreal scale = 1.0) const;
    void paintDrawings(QPainter& painter) const;
    void paintDraggedPiece(QPainter& painter);
    void paintPromotion(QPainter& painter);

    core::Position m_position;
    std::optional<core::Move> m_lastMove;
    std::vector<core::Move> m_legalMoves;
    std::vector<core::Arrow> m_arrows;
    std::vector<core::SquareMark> m_marks;
    std::vector<core::Move> m_candidates;
    int m_candidateChoice = -1;

    BoardTheme m_theme;
    PieceSet m_pieces;
    BoardGeometry m_geometry;
    bool m_flipped = false;
    bool m_showCoordinates = true;
    bool m_interactive = true;

    // Interaction state
    std::optional<core::Square> m_selected;
    std::optional<core::Square> m_pressSquare; ///< left button went down on a pickable piece
    bool m_dragging = false;
    bool m_wasSelected = false; ///< the pressed piece was already selected
    QPoint m_pressPoint;
    QPoint m_mousePoint;
    std::optional<Promotion> m_promotion;
    std::optional<core::Square> m_drawFrom; ///< right button went down here
    std::optional<core::Square> m_drawTo;

    // Slide animation of the last move
    std::vector<Slide> m_slides;
    bool m_skipNextSlide = false; ///< a dropped piece is already where it belongs
    qreal m_slideProgress = 1.0;
    QVariantAnimation* m_animation = nullptr;
};

} // namespace chessgui::app
