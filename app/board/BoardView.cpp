#include "BoardView.hpp"

#include <QApplication>
#include <QEasingCurve>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QVariantAnimation>

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>
#include <utility>

namespace chessgui::app {
namespace {

using core::Color;
using core::MarkColor;
using core::Move;
using core::Piece;
using core::PieceType;
using core::Square;

constexpr int kMargin = 8;
constexpr int kSlideMs = 160;
constexpr qreal kCornerRadius = 6.0;

// Highlight colours shared by all themes (semi-transparent, so they tint any square colour).
// Move hints are neutral (a soft shadow tone) so they suit every board colour.
const QColor kMoveHint(0, 0, 0, 38);
const QColor kCandidate(0x4A, 0x4A, 0x4A, 80);
const QColor kChosenCandidate(0x00, 0x30, 0x88, 135);
const QColor kPromotionShade(0, 0, 0, 120);
// Engine suggestions: the best move in Lichess's engine blue, the others fainter.
const QColor kEngineBest(0x00, 0x30, 0x88, 150);
const QColor kEngineOther(0x00, 0x30, 0x88, 70);

// Lichess's drawing colours, semi-transparent so the pieces show through.
constexpr int kDrawingAlpha = 158;
QColor markColor(MarkColor color) {
    switch (color) {
    case MarkColor::Green:
        return {0x15, 0x78, 0x1B, kDrawingAlpha};
    case MarkColor::Red:
        return {0x88, 0x20, 0x20, kDrawingAlpha};
    case MarkColor::Blue:
        return {0x00, 0x30, 0x88, kDrawingAlpha};
    case MarkColor::Yellow:
        return {0xE6, 0x8F, 0x00, kDrawingAlpha};
    }
    return {0x15, 0x78, 0x1B, kDrawingAlpha};
}

// The promotion picker lists pieces in this order, as on Lichess.
constexpr std::array<PieceType, 4> kPromotionPieces{PieceType::Queen, PieceType::Knight,
                                                    PieceType::Rook, PieceType::Bishop};

} // namespace

BoardView::BoardView(QWidget* parent)
    : QWidget(parent), m_theme(BoardTheme::presets().front()),
      m_animation(new QVariantAnimation(this)) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_legalMoves = m_position.legalMoves();

    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0);
    m_animation->setDuration(kSlideMs);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_slideProgress = value.toReal();
        update();
    });
    connect(m_animation, &QVariantAnimation::finished, this, [this] {
        m_slides.clear();
        update();
    });
}

BoardView::~BoardView() = default;

// ---- Public setters ----------------------------------------------------------------------------

void BoardView::setPosition(const core::Position& position, std::optional<Move> lastMove,
                            bool animate) {
    m_position = position;
    m_legalMoves = m_position.legalMoves();
    m_lastMove = lastMove;
    m_selected.reset();
    m_pressSquare.reset();
    m_dragging = false;
    m_promotion.reset();

    m_animation->stop();
    m_slides.clear();
    if (animate && lastMove && !m_skipNextSlide) {
        m_slides.push_back({lastMove->from, lastMove->to});
        // Castling: the rook moves too.
        const Piece moved = m_position.pieceAt(lastMove->to);
        if (moved.type == PieceType::King &&
            std::abs(lastMove->to.file() - lastMove->from.file()) == 2) {
            const int rank = lastMove->to.rank();
            const bool kingSide = lastMove->to.file() == 6;
            m_slides.push_back(
                {Square::at(kingSide ? 7 : 0, rank), Square::at(kingSide ? 5 : 3, rank)});
        }
        m_slideProgress = 0.0;
        m_animation->start();
    }
    m_skipNextSlide = false;
    update();
}

void BoardView::setEngineMoves(std::vector<core::Move> moves) {
    if (moves.size() == m_engineMoves.size() &&
        std::equal(moves.begin(), moves.end(), m_engineMoves.begin(),
                   [](const Move& a, const Move& b) { return a.from == b.from && a.to == b.to; })) {
        return; // nothing to repaint (the engine reports the same moves many times a second)
    }
    m_engineMoves = std::move(moves);
    update();
}

void BoardView::setDrawings(std::vector<core::Arrow> arrows, std::vector<core::SquareMark> marks) {
    m_arrows = std::move(arrows);
    m_marks = std::move(marks);
    update();
}

void BoardView::setCandidates(std::vector<Move> moves, int chosen) {
    m_candidates = std::move(moves);
    m_candidateChoice = chosen;
    update();
}

void BoardView::setFlipped(bool flipped) {
    m_flipped = flipped;
    recomputeGeometry();
    update();
}

void BoardView::setShowCoordinates(bool show) {
    m_showCoordinates = show;
    update();
}

void BoardView::setTheme(const BoardTheme& theme) {
    m_theme = theme;
    update();
}

void BoardView::setPieceSet(const QString& name) {
    m_pieces = PieceSet(name);
    update();
}

QSize BoardView::sizeHint() const {
    return {640, 640};
}

QSize BoardView::minimumSizeHint() const {
    return {240, 240};
}

// ---- Helpers -----------------------------------------------------------------------------------

void BoardView::recomputeGeometry() {
    m_geometry = BoardGeometry(size(), kMargin, m_flipped);
}

void BoardView::resizeEvent(QResizeEvent* /*event*/) {
    recomputeGeometry();
}

void BoardView::select(std::optional<Square> square) {
    m_selected = square;
    update();
}

bool BoardView::canPick(Square square) const {
    const Piece piece = m_position.pieceAt(square);
    return !piece.isNone() && piece.color == m_position.sideToMove();
}

std::vector<Move> BoardView::movesFrom(Square square) const {
    std::vector<Move> result;
    for (const Move& move : m_legalMoves) {
        if (move.from == square) {
            result.push_back(move);
        }
    }
    return result;
}

bool BoardView::tryMove(Square from, Square to) {
    std::vector<Move> candidates;
    for (const Move& move : m_legalMoves) {
        if (move.from == from && move.to == to) {
            candidates.push_back(move);
        }
    }
    if (candidates.empty()) {
        return false;
    }
    if (candidates.front().promotion != PieceType::None) {
        m_promotion = Promotion{from, to}; // ask which piece first
        m_selected.reset();
        update();
        return true;
    }
    emit moveMade(candidates.front()); // the owner calls setPosition(), which resets our state
    return true;
}

MarkColor BoardView::colorForModifiers(Qt::KeyboardModifiers modifiers) {
    const bool shift = (modifiers & Qt::ShiftModifier) != 0;
    const bool alt = (modifiers & Qt::AltModifier) != 0;
    if (shift && alt) {
        return MarkColor::Yellow;
    }
    if (shift) {
        return MarkColor::Red;
    }
    if (alt) {
        return MarkColor::Blue;
    }
    return MarkColor::Green;
}

QRect BoardView::promotionTile(std::size_t index) const {
    if (!m_promotion) {
        return {};
    }
    // Tiles stack from the promotion square towards the middle of the board.
    const QRect target = m_geometry.squareRect(m_promotion->to);
    const int step = m_geometry.row(m_promotion->to) == 0 ? 1 : -1;
    return target.translated(0, step * static_cast<int>(index) * m_geometry.squareSize());
}

std::optional<PieceType> BoardView::promotionChoiceAt(QPoint point) const {
    for (std::size_t i = 0; i < kPromotionPieces.size(); ++i) {
        if (promotionTile(i).contains(point)) {
            return kPromotionPieces[i];
        }
    }
    return std::nullopt;
}

void BoardView::updateCursor(QPoint point) {
    if (m_dragging) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    const auto square = m_geometry.squareAt(point);
    if (!m_promotion && square && canPick(*square)) {
        setCursor(Qt::OpenHandCursor);
    } else if (m_promotion && promotionChoiceAt(point)) {
        setCursor(Qt::PointingHandCursor);
    } else {
        unsetCursor();
    }
}

// ---- Mouse and keyboard ------------------------------------------------------------------------

void BoardView::mousePressEvent(QMouseEvent* event) {
    if (!m_interactive) {
        return;
    }
    const QPoint point = event->position().toPoint();
    const auto square = m_geometry.squareAt(point);
    m_mousePoint = point;

    if (event->button() == Qt::RightButton) {
        m_promotion.reset();
        m_dragging = false;
        m_pressSquare.reset();
        m_drawFrom = square;
        m_drawTo = square;
        update();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }

    // A left click anywhere on the board wipes the drawings, as on Lichess.
    if (!m_arrows.empty() || !m_marks.empty()) {
        m_arrows.clear();
        m_marks.clear();
        emit drawingsChanged(m_arrows, m_marks);
        update();
    }

    if (m_promotion) {
        const auto choice = promotionChoiceAt(point);
        const Promotion pending = *m_promotion;
        m_promotion.reset();
        if (choice) {
            emit moveMade(Move{pending.from, pending.to, *choice});
        } else {
            update(); // clicked elsewhere: cancel
        }
        return;
    }
    if (!square) {
        select(std::nullopt);
        return;
    }
    if (m_selected && *m_selected != *square && tryMove(*m_selected, *square)) {
        return;
    }
    if (canPick(*square)) {
        m_pressSquare = square;
        m_pressPoint = point;
        m_dragging = false;
        // Clicking the selected piece again deselects it (on release, if it wasn't dragged).
        m_wasSelected = m_selected == square;
        select(square);
    } else {
        select(std::nullopt);
    }
}

void BoardView::mouseMoveEvent(QMouseEvent* event) {
    if (!m_interactive) {
        return;
    }
    const QPoint point = event->position().toPoint();
    m_mousePoint = point;

    if (m_drawFrom) {
        const auto square = m_geometry.squareAt(point);
        if (square != m_drawTo) {
            m_drawTo = square;
            update();
        }
        return;
    }
    if (m_pressSquare && (event->buttons() & Qt::LeftButton) != 0 && !m_dragging &&
        (point - m_pressPoint).manhattanLength() >= QApplication::startDragDistance()) {
        m_dragging = true;
    }
    if (m_dragging) {
        update();
    }
    updateCursor(point);
}

void BoardView::mouseReleaseEvent(QMouseEvent* event) {
    if (!m_interactive) {
        return;
    }
    const QPoint point = event->position().toPoint();
    const auto square = m_geometry.squareAt(point);

    if (event->button() == Qt::RightButton && m_drawFrom) {
        const Square from = *m_drawFrom;
        m_drawFrom.reset();
        m_drawTo.reset();
        if (square) {
            const MarkColor color = colorForModifiers(event->modifiers());
            if (*square == from) {
                core::toggleSquareMark(m_marks, {from, color});
            } else {
                core::toggleArrow(m_arrows, {from, *square, color});
            }
            emit drawingsChanged(m_arrows, m_marks);
        }
        update();
        return;
    }
    if (event->button() != Qt::LeftButton || !m_pressSquare) {
        return;
    }

    const Square from = *m_pressSquare;
    m_pressSquare.reset();
    if (m_dragging) {
        m_dragging = false;
        if (square && *square != from) {
            m_skipNextSlide = true; // the piece is already under the cursor
            if (tryMove(from, *square)) {
                updateCursor(point);
                return;
            }
            m_skipNextSlide = false;
        }
        if (!square || *square != from) {
            select(std::nullopt); // dropped somewhere useless: put the piece back
        }
    } else if (m_wasSelected) {
        select(std::nullopt);
    }
    update();
    updateCursor(point);
}

void BoardView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && (m_promotion || m_selected || m_dragging)) {
        m_promotion.reset();
        m_selected.reset();
        m_pressSquare.reset();
        m_dragging = false;
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

void BoardView::leaveEvent(QEvent* event) {
    if (!m_dragging) {
        unsetCursor();
    }
    QWidget::leaveEvent(event);
}

// ---- Painting ----------------------------------------------------------------------------------

void BoardView::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    // Squares and highlights are clipped to a board with softly rounded corners.
    QPainterPath board;
    board.addRoundedRect(QRectF(m_geometry.boardRect()), kCornerRadius, kCornerRadius);
    painter.setClipPath(board);
    paintSquares(painter);
    paintHighlights(painter);
    painter.setClipping(false);
    paintCoordinates(painter);
    paintPieces(painter);
    paintMoveHints(painter);
    paintDrawings(painter);
    paintDraggedPiece(painter);
    paintPromotion(painter);
}

void BoardView::paintSquares(QPainter& painter) const {
    for (int index = 0; index < 64; ++index) {
        const Square square = Square::fromIndex(index);
        const bool light = (square.file() + square.rank()) % 2 == 1;
        painter.fillRect(m_geometry.squareRect(square), light ? m_theme.light : m_theme.dark);
    }
}

void BoardView::paintHighlights(QPainter& painter) const {
    if (m_lastMove) {
        painter.fillRect(m_geometry.squareRect(m_lastMove->from), m_theme.highlight);
        painter.fillRect(m_geometry.squareRect(m_lastMove->to), m_theme.highlight);
    }
    if (m_selected) {
        // The picked-up piece's square: the highlight colour, a little stronger.
        QColor selected = m_theme.highlight;
        selected.setAlphaF(std::min(1.0F, selected.alphaF() * 1.45F));
        painter.fillRect(m_geometry.squareRect(*m_selected), selected);
    }
    // Check: a red glow under the king that is in check.
    if (m_position.inCheck()) {
        for (int index = 0; index < 64; ++index) {
            const Square square = Square::fromIndex(index);
            const Piece piece = m_position.pieceAt(square);
            if (piece.type == PieceType::King && piece.color == m_position.sideToMove()) {
                const QRect rect = m_geometry.squareRect(square);
                QRadialGradient glow(QRectF(rect).center(), rect.width() * 0.6);
                glow.setColorAt(0.0, QColor(255, 0, 0, 255));
                glow.setColorAt(0.25, QColor(231, 0, 0, 255));
                glow.setColorAt(0.9, QColor(169, 0, 0, 0));
                painter.fillRect(rect, glow);
            }
        }
    }
}

void BoardView::paintCoordinates(QPainter& painter) const {
    if (!m_showCoordinates) {
        return;
    }
    const int size = m_geometry.squareSize();
    QFont font = painter.font();
    font.setPixelSize(std::max(9, size / 6));
    font.setBold(true);
    painter.setFont(font);
    const int pad = std::max(2, size / 20);

    for (int i = 0; i < 8; ++i) {
        // Files along the bottom row, ranks along the left column (as seen on screen).
        const Square fileSquare = m_flipped ? Square::at(7 - i, 7) : Square::at(i, 0);
        const Square rankSquare = m_flipped ? Square::at(7, i) : Square::at(0, 7 - i);
        for (const auto& [square, text, align] :
             {std::tuple{fileSquare, QString(QChar('a' + fileSquare.file())),
                         Qt::AlignRight | Qt::AlignBottom},
              std::tuple{rankSquare, QString::number(rankSquare.rank() + 1),
                         Qt::AlignLeft | Qt::AlignTop}}) {
            const bool light = (square.file() + square.rank()) % 2 == 1;
            painter.setPen(light ? m_theme.dark : m_theme.light);
            painter.drawText(m_geometry.squareRect(square).adjusted(pad, pad, -pad, -pad),
                             static_cast<int>(align), text);
        }
    }
}

void BoardView::paintPieces(QPainter& painter) {
    const int size = m_geometry.squareSize();
    const qreal ratio = devicePixelRatioF();
    for (int index = 0; index < 64; ++index) {
        const Square square = Square::fromIndex(index);
        const Piece piece = m_position.pieceAt(square);
        if (piece.isNone()) {
            continue;
        }
        const bool sliding = std::any_of(m_slides.begin(), m_slides.end(),
                                         [&](const Slide& s) { return s.to == square; });
        if (sliding) {
            continue; // drawn below at its in-between position
        }
        const QPixmap pixmap = m_pieces.pixmap(piece, size, ratio);
        if (m_dragging && m_pressSquare == square) {
            painter.setOpacity(0.3); // ghost left behind while dragging
            painter.drawPixmap(m_geometry.squareRect(square).topLeft(), pixmap);
            painter.setOpacity(1.0);
            continue;
        }
        painter.drawPixmap(m_geometry.squareRect(square).topLeft(), pixmap);
    }
    for (const Slide& slide : m_slides) {
        const QPointF from = m_geometry.squareRect(slide.from).topLeft();
        const QPointF to = m_geometry.squareRect(slide.to).topLeft();
        const QPointF at = from + ((to - from) * m_slideProgress);
        painter.drawPixmap(at, m_pieces.pixmap(m_position.pieceAt(slide.to), size, ratio));
    }
}

void BoardView::paintMoveHints(QPainter& painter) const {
    const auto from = m_dragging ? m_pressSquare : m_selected;
    if (!from || m_promotion) {
        return;
    }
    const qreal size = m_geometry.squareSize();
    const auto hover = m_dragging ? m_geometry.squareAt(m_mousePoint) : std::nullopt;
    for (const Move& move : movesFrom(*from)) {
        const QRectF rect(m_geometry.squareRect(move.to));
        if (m_position.pieceAt(move.to).isNone()) {
            // A quiet move: a small dot in the middle of the square.
            painter.setPen(Qt::NoPen);
            painter.setBrush(kMoveHint);
            painter.drawEllipse(rect.center(), size * 0.16, size * 0.16);
        } else {
            // A capture: a thin ring just inside the square, around the piece.
            const qreal width = size * 0.085;
            painter.setPen(QPen(kMoveHint, width));
            painter.setBrush(Qt::NoBrush);
            const qreal radius = (size - width) / 2;
            painter.drawEllipse(rect.center(), radius, radius);
        }
        if (hover == move.to) {
            // The square a dragged piece would land on: an inset frame in the highlight colour,
            // made nearly opaque so it shows on light and dark squares alike.
            const qreal frame = std::max(2.0, size * 0.055);
            QColor color = m_theme.highlight;
            color.setAlphaF(0.85F);
            painter.setPen(QPen(color, frame));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(rect.adjusted(frame / 2, frame / 2, -frame / 2, -frame / 2));
        }
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::NoBrush);
}

void BoardView::paintArrow(QPainter& painter, Square from, Square to, const QColor& color,
                           qreal scale) const {
    // `scale` makes thinner arrows (candidate moves) with the same shape.
    const qreal size = m_geometry.squareSize();
    // Knight moves bend into an L (as on chess.com): along the long leg first, then across.
    std::vector<QPointF> points{m_geometry.squareCenter(from)};
    const int df = to.file() - from.file();
    const int dr = to.rank() - from.rank();
    if ((std::abs(df) == 1 && std::abs(dr) == 2) || (std::abs(df) == 2 && std::abs(dr) == 1)) {
        const Square corner = std::abs(dr) == 2 ? Square::at(from.file(), to.rank())
                                                : Square::at(to.file(), from.rank());
        points.push_back(m_geometry.squareCenter(corner));
    }
    const QPointF end = m_geometry.squareCenter(to);

    // The head points along the last leg; its tip stops just short of the target's centre.
    const QPointF lastLeg = end - points.back();
    const qreal legLength = std::hypot(lastLeg.x(), lastLeg.y());
    if (legLength < 1) {
        return;
    }
    const QPointF dir = lastLeg / legLength;
    const QPointF normal(-dir.y(), dir.x());
    const qreal headLength = size * 0.40 * scale;
    const qreal headWidth = size * 0.50 * scale;
    const QPointF tip = end - (dir * size * 0.10);
    const QPointF base = tip - (dir * headLength);
    points.push_back(base + (dir * 1.0)); // overlap the head by a pixel: no seam

    // One outline for shaft and head, filled once with a translucent colour: the arrow reads as
    // a single shape, with no darker seam where shaft and head meet.
    QPainterPath shaft(points.front());
    for (std::size_t i = 1; i < points.size(); ++i) {
        shaft.lineTo(points[i]);
    }
    QPainterPathStroker stroker;
    stroker.setWidth(size * 0.17 * scale);
    stroker.setCapStyle(Qt::FlatCap);
    stroker.setJoinStyle(Qt::MiterJoin);
    QPainterPath outline = stroker.createStroke(shaft);
    QPainterPath head;
    head.moveTo(tip);
    head.lineTo(base + (normal * headWidth / 2));
    head.lineTo(base - (normal * headWidth / 2));
    head.closeSubpath();
    outline = outline.united(head);

    painter.setPen(Qt::NoPen);
    painter.fillPath(outline, color);
}

void BoardView::paintDrawings(QPainter& painter) const {
    const qreal size = m_geometry.squareSize();
    // The engine's moves underneath everything else; the best one last, so it's on top.
    for (std::size_t i = m_engineMoves.size(); i > 1; --i) {
        const Move& move = m_engineMoves[i - 1];
        if (move.from != m_engineMoves.front().from || move.to != m_engineMoves.front().to) {
            paintArrow(painter, move.from, move.to, kEngineOther, 0.6);
        }
    }
    if (!m_engineMoves.empty()) {
        paintArrow(painter, m_engineMoves.front().from, m_engineMoves.front().to, kEngineBest);
    }
    // Candidate continuations at a branch point: thin pale arrows, the chosen one in blue
    // (as on Lichess). Drawn first, so the user's own drawings stay on top.
    for (std::size_t i = 0; i < m_candidates.size(); ++i) {
        if (!std::cmp_equal(i, m_candidateChoice)) {
            paintArrow(painter, m_candidates[i].from, m_candidates[i].to, kCandidate, 0.7);
        }
    }
    if (m_candidateChoice >= 0 && std::cmp_less(m_candidateChoice, m_candidates.size())) {
        const Move& chosen = m_candidates[static_cast<std::size_t>(m_candidateChoice)];
        paintArrow(painter, chosen.from, chosen.to, kChosenCandidate, 0.8);
    }

    const auto paintMark = [&](Square square, MarkColor color) {
        painter.setPen(QPen(markColor(color), size * 0.08));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(m_geometry.squareCenter(square), size * 0.46, size * 0.46);
    };
    for (const auto& mark : m_marks) {
        paintMark(mark.square, mark.color);
    }
    for (const auto& arrow : m_arrows) {
        paintArrow(painter, arrow.from, arrow.to, markColor(arrow.color));
    }
    if (m_drawFrom && m_drawTo) { // the arrow or mark being drawn right now
        const MarkColor color = colorForModifiers(QGuiApplication::queryKeyboardModifiers());
        if (*m_drawFrom == *m_drawTo) {
            paintMark(*m_drawFrom, color);
        } else {
            paintArrow(painter, *m_drawFrom, *m_drawTo, markColor(color));
        }
    }
}

void BoardView::paintDraggedPiece(QPainter& painter) {
    if (!m_dragging || !m_pressSquare) {
        return;
    }
    const int size = m_geometry.squareSize();
    const QPixmap pixmap =
        m_pieces.pixmap(m_position.pieceAt(*m_pressSquare), size, devicePixelRatioF());
    painter.drawPixmap(m_mousePoint - QPoint(size / 2, size / 2), pixmap);
}

void BoardView::paintPromotion(QPainter& painter) {
    if (!m_promotion) {
        return;
    }
    painter.fillRect(m_geometry.boardRect(), kPromotionShade);
    const int size = m_geometry.squareSize();
    const Color mover = m_position.sideToMove();
    for (std::size_t i = 0; i < kPromotionPieces.size(); ++i) {
        const QRect tile = promotionTile(i);
        const bool hovered = tile.contains(m_mousePoint);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hovered ? QColor(255, 255, 255) : QColor(235, 235, 235));
        painter.drawEllipse(
            QRectF(tile).adjusted(size * 0.04, size * 0.04, -size * 0.04, -size * 0.04));
        const QPixmap pixmap =
            m_pieces.pixmap({kPromotionPieces[i], mover}, size, devicePixelRatioF());
        painter.drawPixmap(tile.topLeft(), pixmap);
    }
    painter.setBrush(Qt::NoBrush);
}

} // namespace chessgui::app
