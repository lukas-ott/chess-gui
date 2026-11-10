#include "board/BoardView.hpp"

#include <QApplication>
#include <QDir>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>
#include <gtest/gtest.h>

namespace {

using chessgui::app::BoardTheme;
using chessgui::app::BoardView;
using namespace chessgui::core;

Square sq(std::string_view name) {
    return Square::parse(name).value();
}

Position fromFen(std::string_view fen) {
    return Position::fromFen(fen).value();
}

/// A shown 400×400 board (50 px squares) with a recorder for its signals.
class BoardViewTest : public ::testing::Test {
protected:
    void SetUp() override {
        board.resize(416, 416); // 8 px margin on each side, 50 px squares
        board.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&board));
    }

    QPoint center(std::string_view square) const { return board.squareRect(sq(square)).center(); }

    void send(QEvent::Type type, QPoint point, Qt::MouseButton button, Qt::MouseButtons buttons,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QMouseEvent event(type, QPointF(point), board.mapToGlobal(QPointF(point)), button, buttons,
                          modifiers);
        QApplication::sendEvent(&board, &event);
    }
    void click(std::string_view square, Qt::MouseButton button = Qt::LeftButton,
               Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        send(QEvent::MouseButtonPress, center(square), button, button, modifiers);
        send(QEvent::MouseButtonRelease, center(square), button, Qt::NoButton, modifiers);
    }
    void drag(std::string_view from, std::string_view to, Qt::MouseButton button = Qt::LeftButton,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        send(QEvent::MouseButtonPress, center(from), button, button, modifiers);
        send(QEvent::MouseMove, (center(from) + center(to)) / 2, Qt::NoButton, button, modifiers);
        send(QEvent::MouseMove, center(to), Qt::NoButton, button, modifiers);
        send(QEvent::MouseButtonRelease, center(to), button, Qt::NoButton, modifiers);
    }
    Move lastMove() const { return moves.takeLast().at(0).value<Move>(); }

    BoardView board;
    mutable QSignalSpy moves{&board, &BoardView::moveMade};
    QSignalSpy drawings{&board, &BoardView::drawingsChanged};
};

TEST_F(BoardViewTest, ClickClickPlaysAMove) {
    click("e2");
    click("e4");
    ASSERT_EQ(moves.count(), 1);
    EXPECT_EQ(lastMove().uci(), "e2e4");
}

TEST_F(BoardViewTest, DragAndDropPlaysAMove) {
    drag("g1", "f3");
    ASSERT_EQ(moves.count(), 1);
    EXPECT_EQ(lastMove().uci(), "g1f3");
}

TEST_F(BoardViewTest, IllegalMovesDoNothing) {
    click("e2");
    click("e5");
    drag("d2", "d5");
    click("e7"); // Black's piece, but White is to move
    click("e5");
    EXPECT_EQ(moves.count(), 0);
}

TEST_F(BoardViewTest, ClickingAnotherOwnPieceSwitchesSelection) {
    click("e2");
    click("d2");
    click("d4");
    ASSERT_EQ(moves.count(), 1);
    EXPECT_EQ(lastMove().uci(), "d2d4");
}

TEST_F(BoardViewTest, PromotionAsksForThePiece) {
    board.setPosition(fromFen("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1"));
    click("b7");
    click("b8");
    EXPECT_EQ(moves.count(), 0); // waiting for the choice
    // The picker stacks queen, knight, rook, bishop downwards from b8.
    click("b7");
    ASSERT_EQ(moves.count(), 1);
    EXPECT_EQ(lastMove().uci(), "b7b8n");
}

TEST_F(BoardViewTest, PromotionCanBeCancelled) {
    board.setPosition(fromFen("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1"));
    drag("b7", "b8");
    click("h1"); // outside the picker
    EXPECT_EQ(moves.count(), 0);
    click("b7");
    click("b8");
    click("b8"); // queen
    ASSERT_EQ(moves.count(), 1);
    EXPECT_EQ(lastMove().uci(), "b7b8q");
}

TEST_F(BoardViewTest, FlippedBoardMapsClicksCorrectly) {
    board.setFlipped(true);
    click("e2");
    click("e4");
    ASSERT_EQ(moves.count(), 1);
    EXPECT_EQ(lastMove().uci(), "e2e4");
}

TEST_F(BoardViewTest, RightDragDrawsArrowsInModifierColours) {
    drag("e2", "e4", Qt::RightButton);
    drag("d1", "h5", Qt::RightButton, Qt::ShiftModifier);
    drag("g1", "f3", Qt::RightButton, Qt::AltModifier);
    drag("b1", "c3", Qt::RightButton, Qt::ShiftModifier | Qt::AltModifier);
    EXPECT_EQ(drawings.count(), 4);
    ASSERT_EQ(board.arrows().size(), 4U);
    EXPECT_EQ(board.arrows()[0], (Arrow{sq("e2"), sq("e4"), MarkColor::Green}));
    EXPECT_EQ(board.arrows()[1].color, MarkColor::Red);
    EXPECT_EQ(board.arrows()[2].color, MarkColor::Blue);
    EXPECT_EQ(board.arrows()[3].color, MarkColor::Yellow);

    drag("e2", "e4", Qt::RightButton); // same arrow again removes it
    EXPECT_EQ(board.arrows().size(), 3U);
    EXPECT_EQ(moves.count(), 0); // drawing never plays moves
}

TEST_F(BoardViewTest, RightClickMarksASquare) {
    click("d5", Qt::RightButton);
    ASSERT_EQ(board.squareMarks().size(), 1U);
    EXPECT_EQ(board.squareMarks()[0], (SquareMark{sq("d5"), MarkColor::Green}));
    click("d5", Qt::RightButton);
    EXPECT_TRUE(board.squareMarks().empty());
}

TEST_F(BoardViewTest, LeftClickClearsTheDrawings) {
    drag("e2", "e4", Qt::RightButton);
    click("d5", Qt::RightButton);
    ASSERT_EQ(board.arrows().size(), 1U);
    ASSERT_EQ(board.squareMarks().size(), 1U);

    click("e2"); // selecting a piece also clears
    EXPECT_TRUE(board.arrows().empty());
    EXPECT_TRUE(board.squareMarks().empty());
    EXPECT_EQ(drawings.count(), 3); // two drawn, one cleared
    click("e4");
    EXPECT_EQ(moves.count(), 1); // the click still did its normal job
}

TEST_F(BoardViewTest, NewPositionClearsTheSelection) {
    click("e2");
    board.setPosition(Position());
    click("e4"); // e2 is no longer selected, so this does nothing
    EXPECT_EQ(moves.count(), 0);
}

/// Writes PNGs of the board in various states when CHESSGUI_SNAPSHOT_DIR is set, so the
/// rendering can be looked at. Always checks that painting works.
TEST_F(BoardViewTest, Snapshots) {
    const QString dir = qEnvironmentVariable("CHESSGUI_SNAPSHOT_DIR");
    const auto save = [&](const QString& name) {
        const QImage image = board.grab().toImage();
        EXPECT_FALSE(image.isNull());
        if (!dir.isEmpty()) {
            image.save(QDir(dir).filePath(name + QStringLiteral(".png")));
        }
    };

    Position position;
    for (const char* san : {"e4", "e5", "Nf3", "Nc6", "Bc4", "Bc5", "Qe2", "d6"}) {
        position.play(position.parseSan(san).value());
    }
    const Move last = position.parseSan("O-O").value();
    position.play(last);
    board.setPosition(position, last);
    board.setDrawings({{sq("f3"), sq("g5"), MarkColor::Green},
                       {sq("c5"), sq("f2"), MarkColor::Red},
                       {sq("d1"), sq("h5"), MarkColor::Blue}},
                      {{sq("f7"), MarkColor::Red}, {sq("d5"), MarkColor::Yellow}});
    save(QStringLiteral("drawings"));

    board.setPosition(
        Position::fromFen("r1bqk1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4")
            .value());
    click("f3");
    save(QStringLiteral("selected"));

    board.setPosition(fromFen("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1"));
    click("b7");
    click("b8");
    save(QStringLiteral("promotion"));

    board.setPosition(fromFen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3"));
    board.setFlipped(true);
    save(QStringLiteral("check-flipped"));

    board.setFlipped(false);
    board.setPosition(Position());
    for (const auto& theme : BoardTheme::presets()) {
        board.setTheme(theme);
        save(QStringLiteral("theme-") + theme.name);
    }
    for (const auto& name : chessgui::app::PieceSet::available()) {
        board.setPieceSet(name);
        save(QStringLiteral("pieces-") + name);
    }
}

/// Interaction states for design review (saved when CHESSGUI_SNAPSHOT_DIR is set): dragging
/// over a capture and over an empty square, a selection with captures, and many arrow shapes.
TEST_F(BoardViewTest, DesignStates) {
    const QString dir = qEnvironmentVariable("CHESSGUI_SNAPSHOT_DIR");
    const auto save = [&](const QString& name) {
        const QImage image = board.grab().toImage();
        EXPECT_FALSE(image.isNull());
        if (!dir.isEmpty()) {
            image.save(
                QDir(dir).filePath(QStringLiteral("state-") + name + QStringLiteral(".png")));
        }
    };
    // A middlegame with captures available for the knight on f3.
    const Position middlegame =
        fromFen("r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/3P1N2/PPP2PPP/RNBQK2R w KQkq - 1 5");
    board.setPosition(middlegame);

    // Dragging the knight, hovering over the e5 pawn (a capture).
    send(QEvent::MouseButtonPress, center("f3"), Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseMove, center("f4"), Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseMove, center("e5") + QPoint(6, 4), Qt::NoButton, Qt::LeftButton);
    save(QStringLiteral("drag-capture"));
    send(QEvent::MouseMove, center("h4") + QPoint(-5, 3), Qt::NoButton, Qt::LeftButton);
    save(QStringLiteral("drag-empty"));
    send(QEvent::MouseButtonRelease, center("f3"), Qt::LeftButton, Qt::NoButton); // put it back

    // The bishop selected: captures on f7 and quiet moves.
    board.setPosition(middlegame);
    click("c4");
    save(QStringLiteral("selected-bishop"));

    // Arrows of every kind: long, short, diagonal, knight both ways, overlapping, all colours.
    board.setPosition(middlegame);
    board.setDrawings({{sq("f3"), sq("g5"), MarkColor::Green},
                       {sq("b1"), sq("d2"), MarkColor::Green},
                       {sq("c4"), sq("f7"), MarkColor::Red},
                       {sq("d1"), sq("d2"), MarkColor::Blue},
                       {sq("h2"), sq("h4"), MarkColor::Yellow},
                       {sq("a2"), sq("a7"), MarkColor::Blue},
                       {sq("e1"), sq("g1"), MarkColor::Green}},
                      {{sq("e5"), MarkColor::Red}, {sq("d5"), MarkColor::Green}});
    save(QStringLiteral("arrows"));
    board.setDrawings({}, {});
}

} // namespace
