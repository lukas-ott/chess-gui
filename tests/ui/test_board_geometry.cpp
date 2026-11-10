#include "board/BoardGeometry.hpp"

#include <gtest/gtest.h>

namespace {

using kiebitz::app::BoardGeometry;
using kiebitz::core::Square;

Square sq(std::string_view name) {
    return Square::parse(name).value();
}

TEST(BoardGeometry, FitsAndCentresAWholePixelBoard) {
    const BoardGeometry geometry(QSize(500, 420), 10, false);
    EXPECT_EQ(geometry.squareSize(), 50); // (420 - 2*10) / 8
    EXPECT_EQ(geometry.boardRect(), QRect(50, 10, 400, 400));
}

TEST(BoardGeometry, WhiteAtTheBottom) {
    const BoardGeometry geometry(QSize(400, 400), 0, false);
    EXPECT_EQ(geometry.squareRect(sq("a1")), QRect(0, 350, 50, 50));
    EXPECT_EQ(geometry.squareRect(sq("h8")), QRect(350, 0, 50, 50));
    EXPECT_EQ(geometry.squareAt(QPoint(10, 390)), sq("a1"));
    EXPECT_EQ(geometry.squareAt(QPoint(225, 175)), sq("e5"));
}

TEST(BoardGeometry, FlippedPutsBlackAtTheBottom) {
    const BoardGeometry geometry(QSize(400, 400), 0, true);
    EXPECT_EQ(geometry.squareRect(sq("a1")), QRect(350, 0, 50, 50));
    EXPECT_EQ(geometry.squareRect(sq("h8")), QRect(0, 350, 50, 50));
    EXPECT_EQ(geometry.squareAt(QPoint(10, 390)), sq("h8"));
}

TEST(BoardGeometry, EverySquareRoundTrips) {
    for (const bool flipped : {false, true}) {
        const BoardGeometry geometry(QSize(613, 577), 8, flipped);
        for (int i = 0; i < 64; ++i) {
            const Square square = Square::fromIndex(i);
            EXPECT_EQ(geometry.squareAt(geometry.squareRect(square).center()), square)
                << square.name() << (flipped ? " flipped" : "");
        }
    }
}

TEST(BoardGeometry, OutsideTheBoardIsNoSquare) {
    const BoardGeometry geometry(QSize(500, 400), 0, false);
    EXPECT_FALSE(geometry.squareAt(QPoint(10, 10)).has_value()); // left of the centred board
    EXPECT_FALSE(geometry.squareAt(QPoint(250, 400)).has_value());
}

} // namespace
