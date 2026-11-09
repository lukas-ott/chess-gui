#include "chessgui/core/position.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace chessgui::core;

constexpr std::string_view kKiwipete =
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

Square sq(std::string_view name) {
    return Square::parse(name).value();
}

TEST(Fen, StartPosition) {
    const Position position;
    EXPECT_EQ(position.fen(), kStartFen);
    EXPECT_EQ(position.sideToMove(), Color::White);
    EXPECT_EQ(position.pieceAt(sq("e1")), (Piece{PieceType::King, Color::White}));
    EXPECT_EQ(position.pieceAt(sq("d8")), (Piece{PieceType::Queen, Color::Black}));
    EXPECT_TRUE(position.pieceAt(sq("e4")).isNone());
    EXPECT_EQ(position.castlingRights(), (CastlingRights{true, true, true, true}));
    EXPECT_EQ(position.halfmoveClock(), 0);
    EXPECT_EQ(position.fullmoveNumber(), 1);
}

TEST(Fen, RoundTrip) {
    for (const auto fen :
         {kStartFen, kKiwipete, std::string_view("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"),
          std::string_view("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w "
                           "KQkq - 2 3")}) {
        const auto position = Position::fromFen(fen);
        ASSERT_TRUE(position.ok()) << fen << ": " << position.error().message;
        EXPECT_EQ(position->fen(), fen);
    }
}

TEST(Fen, MoveCountersAreOptional) {
    const auto position = Position::fromFen("4k3/8/8/8/8/8/8/4K3 w - -");
    ASSERT_TRUE(position.ok());
    EXPECT_EQ(position->fen(), "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
}

TEST(Fen, ExtraWhitespaceIsFine) {
    const auto position = Position::fromFen("  4k3/8/8/8/8/8/8/4K3   b  -  -  5  40 ");
    ASSERT_TRUE(position.ok());
    EXPECT_EQ(position->fen(), "4k3/8/8/8/8/8/8/4K3 b - - 5 40");
}

TEST(Fen, ImpossibleCastlingRightsAreDropped) {
    // White's king and rooks have moved, so only Black can still castle.
    const auto position = Position::fromFen("r3k2r/8/8/8/8/8/8/4K3 w KQkq - 0 1");
    ASSERT_TRUE(position.ok());
    EXPECT_EQ(position->castlingRights(), (CastlingRights{false, false, true, true}));
}

TEST(Fen, EnPassantSquare) {
    // After 1. e4 no black pawn can capture on e3, so the square is dropped.
    const auto noCapture =
        Position::fromFen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    ASSERT_TRUE(noCapture.ok());
    EXPECT_FALSE(noCapture->enPassantSquare().has_value());

    const auto capture = Position::fromFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    ASSERT_TRUE(capture.ok());
    EXPECT_EQ(capture->enPassantSquare(), sq("d6"));
}

TEST(Fen, ZeroMoveNumberBecomesOne) {
    const auto position = Position::fromFen("4k3/8/8/8/8/8/8/4K3 w - - 0 0");
    ASSERT_TRUE(position.ok());
    EXPECT_EQ(position->fullmoveNumber(), 1);
}

struct BadFen {
    const char* name; // test name shown by ctest
    const char* fen;
    const char* messagePart;
};

class FenErrors : public ::testing::TestWithParam<BadFen> {};

TEST_P(FenErrors, AreRejectedWithAClearMessage) {
    const auto position = Position::fromFen(GetParam().fen);
    ASSERT_FALSE(position.ok()) << GetParam().fen;
    EXPECT_NE(position.error().message.find(GetParam().messagePart), std::string::npos)
        << "message was: " << position.error().message;
}

INSTANTIATE_TEST_SUITE_P(
    Fen, FenErrors,
    ::testing::Values(
        BadFen{"Empty", "", "4 to 6 fields"},
        BadFen{"TwoFields", "4k3/8/8/8/8/8/8/4K3 w", "4 to 6 fields"},
        BadFen{"SevenFields", "4k3/8/8/8/8/8/8/4K3 w - - 0 1 extra", "4 to 6 fields"},
        BadFen{"RankTooLong", "4k3/8/8/8/8/8/8/4K4 w - - 0 1", "more than 8 squares"},
        BadFen{"RankOneShort", "4k3/8/8/8/8/8/8/4K2 w - - 0 1", "Rank 1 has 7 squares"},
        BadFen{"RankSevenShort", "4k3/7/8/8/8/8/8/4K3 w - - 0 1", "Rank 7 has 7 squares"},
        BadFen{"SevenRanks", "4k3/8/8/8/8/8/4K3 w - - 0 1", "fewer than 8 ranks"},
        BadFen{"NineRanks", "4k3/8/8/8/8/8/8/8/4K3 w - - 0 1", "more than 8 ranks"},
        BadFen{"BadPieceLetter", "4k3/8/8/8/8/8/8/4X3 w - - 0 1", "'X' is not a piece"},
        BadFen{"TwoWhiteKings", "4k3/8/8/8/8/8/8/3KK3 w - - 0 1", "White must have exactly one"},
        BadFen{"NoBlackKing", "8/8/8/8/8/8/8/4K3 w - - 0 1", "Black must have exactly one"},
        BadFen{"PawnOnLastRank", "P3k3/8/8/8/8/8/8/4K3 w - - 0 1", "pawn on a8"},
        BadFen{"BadSideToMove", "4k3/8/8/8/8/8/8/4K3 x - - 0 1", "side to move"},
        BadFen{"BadCastlingLetter", "4k3/8/8/8/8/8/8/4K3 w KX - 0 1", "castling field"},
        BadFen{"RepeatedCastlingLetter", "4k3/8/8/8/8/8/8/4K3 w KK - 0 1", "castling field"},
        BadFen{"BadEnPassantSquare", "4k3/8/8/8/8/8/8/4K3 w - e9 0 1", "not an en passant square"},
        BadFen{"EnPassantWrongRank", "4k3/8/8/8/8/8/8/4K3 w - e3 0 1", "must be on rank 6"},
        BadFen{"BadHalfmoveClock", "4k3/8/8/8/8/8/8/4K3 w - - a 1", "halfmove clock"},
        BadFen{"NegativeMoveNumber", "4k3/8/8/8/8/8/8/4K3 w - - 0 -1", "move number"},
        BadFen{"OpponentInCheck", "4k3/8/8/8/8/8/8/4R1K1 w - - 0 1", "not to move is in check"}),
    [](const auto& testInfo) { return std::string(testInfo.param.name); });

} // namespace
