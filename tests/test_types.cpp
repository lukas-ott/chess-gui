#include "kiebitz/core/types.hpp"

#include <gtest/gtest.h>

namespace {

using namespace kiebitz::core;

TEST(Square, ParseAndName) {
    const auto e4 = Square::parse("e4");
    ASSERT_TRUE(e4.has_value());
    EXPECT_EQ(e4->file(), 4);
    EXPECT_EQ(e4->rank(), 3);
    EXPECT_EQ(e4->index(), 28);
    EXPECT_EQ(e4->name(), "e4");
    EXPECT_EQ(Square::at(0, 0).name(), "a1");
    EXPECT_EQ(Square::at(7, 7).name(), "h8");
}

TEST(Square, RejectsInvalidNames) {
    for (const char* text : {"", "e", "e9", "i1", "E4", "e44", "4e"}) {
        EXPECT_FALSE(Square::parse(text).has_value()) << text;
    }
}

TEST(Piece, FenChars) {
    EXPECT_EQ((Piece{PieceType::Knight, Color::White}).fenChar(), 'N');
    EXPECT_EQ((Piece{PieceType::Queen, Color::Black}).fenChar(), 'q');
    EXPECT_EQ(Piece{}.fenChar(), '\0');
    EXPECT_EQ(Piece::fromFenChar('k'), (Piece{PieceType::King, Color::Black}));
    EXPECT_EQ(Piece::fromFenChar('P'), (Piece{PieceType::Pawn, Color::White}));
    EXPECT_FALSE(Piece::fromFenChar('x').has_value());
    EXPECT_FALSE(Piece::fromFenChar('1').has_value());
}

TEST(Move, Uci) {
    const auto move = Move::fromUci("e7e8q");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(move->from.name(), "e7");
    EXPECT_EQ(move->to.name(), "e8");
    EXPECT_EQ(move->promotion, PieceType::Queen);
    EXPECT_EQ(move->uci(), "e7e8q");
    EXPECT_EQ(Move::fromUci("g1f3").value().uci(), "g1f3");
    for (const char* text : {"", "e2", "e2e9", "e7e8k", "e7e8p", "e7e8qq"}) {
        EXPECT_FALSE(Move::fromUci(text).has_value()) << text;
    }
}

} // namespace
