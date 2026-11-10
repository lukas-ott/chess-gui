#include "chessgui/core/position.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace chessgui::core;

Position fromFen(std::string_view fen) {
    auto position = Position::fromFen(fen);
    EXPECT_TRUE(position.ok()) << fen;
    return *position;
}

Move uci(std::string_view text) {
    return Move::fromUci(text).value();
}

/// SAN of every legal move must parse back to the same move, two plies deep. These positions
/// cover castling, promotions, en passant, pins and checks (the standard perft test set).
void expectSanRoundTrip(const Position& position, int depth, int& checked) {
    for (const Move& move : position.legalMoves()) {
        const std::string san = position.san(move);
        const auto parsed = position.parseSan(san);
        ASSERT_TRUE(parsed.ok()) << position.fen() << " " << san << ": " << parsed.error().message;
        ASSERT_EQ(*parsed, move) << position.fen() << " " << san;
        ++checked;
        if (depth > 1) {
            Position next = position;
            next.play(move);
            expectSanRoundTrip(next, depth - 1, checked);
        }
    }
}

TEST(San, RoundTripsEveryMoveInTestPositions) {
    int checked = 0;
    for (const auto fen : {
             kStartFen,
             std::string_view(
                 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"),
             std::string_view("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"),
             std::string_view("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"),
             std::string_view("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"),
         }) {
        expectSanRoundTrip(fromFen(fen), 2, checked);
    }
    EXPECT_GT(checked, 4000);
}

TEST(San, SimpleMoves) {
    const Position start;
    EXPECT_EQ(start.san(uci("e2e4")), "e4");
    EXPECT_EQ(start.san(uci("g1f3")), "Nf3");
    EXPECT_EQ(*start.parseSan("e4"), uci("e2e4"));
    EXPECT_EQ(*start.parseSan("Nf3"), uci("g1f3"));
}

TEST(San, Disambiguation) {
    const Position knights = fromFen("4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1");
    EXPECT_EQ(knights.san(uci("b1d2")), "Nbd2");
    EXPECT_EQ(knights.san(uci("f1d2")), "Nfd2");

    const Position rooks = fromFen("4k3/8/8/R7/8/8/8/R3K3 w - - 0 1");
    EXPECT_EQ(rooks.san(uci("a1a3")), "R1a3");
    EXPECT_EQ(rooks.san(uci("a5a3")), "R5a3");

    const Position queens = fromFen("4k3/8/8/8/8/Q7/8/Q1Q1K3 w - - 0 1");
    EXPECT_EQ(queens.san(uci("a1b2")), "Qa1b2");
    EXPECT_EQ(*queens.parseSan("Qa1b2"), uci("a1b2"));
    const auto ambiguous = queens.parseSan("Qab2");
    ASSERT_FALSE(ambiguous.ok());
    EXPECT_NE(ambiguous.error().message.find("ambiguous"), std::string::npos);
}

TEST(San, Castling) {
    const Position position = fromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    EXPECT_EQ(position.san(uci("e1g1")), "O-O");
    EXPECT_EQ(position.san(uci("e1c1")), "O-O-O");
    for (const char* text : {"O-O", "0-0", "O-O+", "O-O!"}) {
        EXPECT_EQ(*position.parseSan(text), uci("e1g1")) << text;
    }
    for (const char* text : {"O-O-O", "0-0-0"}) {
        EXPECT_EQ(*position.parseSan(text), uci("e1c1")) << text;
    }
}

TEST(San, PromotionWithCheck) {
    const Position position = fromFen("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1");
    EXPECT_EQ(position.san(uci("b7b8q")), "b8=Q+");
    EXPECT_EQ(position.san(uci("b7b8n")), "b8=N");
    for (const char* text : {"b8=Q+", "b8=Q", "b8Q", "b8=q", "b8q"}) {
        const auto move = position.parseSan(text);
        ASSERT_TRUE(move.ok()) << text << ": " << move.error().message;
        EXPECT_EQ(*move, uci("b7b8q")) << text;
    }
    EXPECT_EQ(*position.parseSan("b8=B"), uci("b7b8b"));
    const auto missing = position.parseSan("b8");
    ASSERT_FALSE(missing.ok());
    EXPECT_NE(missing.error().message.find("promotion"), std::string::npos);
}

TEST(San, EnPassant) {
    const Position position = fromFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    EXPECT_EQ(position.san(uci("e5d6")), "exd6");
    for (const char* text : {"exd6", "exd6 e.p.", "ed6", "e5xd6"}) {
        const auto move = position.parseSan(text);
        ASSERT_TRUE(move.ok()) << text << ": " << move.error().message;
        EXPECT_EQ(*move, uci("e5d6")) << text;
    }
}

TEST(San, CaptureMarkIsOptional) {
    const Position position = fromFen("4k3/8/8/4p3/8/5N2/8/4K3 w - - 0 1");
    EXPECT_EQ(position.san(uci("f3e5")), "Nxe5");
    EXPECT_EQ(*position.parseSan("Ne5"), uci("f3e5"));
    EXPECT_EQ(*position.parseSan("Nxe5"), uci("f3e5"));
}

TEST(San, CheckmateGetsHash) {
    Position position;
    for (const char* text : {"f3", "e5", "g4"}) {
        position.play(*position.parseSan(text));
    }
    const Move mate = *position.parseSan("Qh4");
    EXPECT_EQ(position.san(mate), "Qh4#");
    position.play(mate);
    EXPECT_TRUE(position.isCheckmate());
    EXPECT_FALSE(position.isStalemate());
}

TEST(San, Annotations) {
    const Position start;
    for (const char* text : {"e4!", "e4?", "e4!!", "e4!?", "e4?!", "e4??"}) {
        EXPECT_EQ(*start.parseSan(text), uci("e2e4")) << text;
    }
}

TEST(San, Errors) {
    const Position start;
    for (const char* text : {"", "Zz9", "e9", "Ke2", "e5", "Nf4", "O-O", "Bb8=Q"}) {
        EXPECT_FALSE(start.parseSan(text).ok()) << text;
    }
    EXPECT_EQ(start.parseSan("Ke2").error().message, "\"Ke2\" is not a legal move here.");
}

TEST(Uci, ParsesLegalMovesOnly) {
    const Position start;
    EXPECT_EQ(*start.parseUci("e2e4"), uci("e2e4"));
    EXPECT_FALSE(start.parseUci("e2e5").ok());
    EXPECT_FALSE(start.parseUci("hello").ok());
}

TEST(Uci, CastlingAsKingTakesRook) {
    // The Lichess cloud and Chess960 engines write castling as the king taking its rook.
    const Position white = fromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    EXPECT_EQ(*white.parseUci("e1h1"), uci("e1g1"));
    EXPECT_EQ(*white.parseUci("e1a1"), uci("e1c1"));
    const Position black = fromFen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
    EXPECT_EQ(*black.parseUci("e8h8"), uci("e8g8"));
    // Without the right to castle it stays illegal.
    EXPECT_FALSE(fromFen("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1").parseUci("e1h1").ok());
}

TEST(PositionState, StalemateAndInsufficientMaterial) {
    EXPECT_TRUE(fromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1").isStalemate());
    EXPECT_TRUE(fromFen("7k/8/6K1/8/8/8/8/8 w - - 0 1").isInsufficientMaterial());
    EXPECT_FALSE(Position().isInsufficientMaterial());
}

TEST(PositionState, HashDependsOnPositionNotHistory) {
    Position a;
    Position b;
    for (const char* text : {"Nf3", "Nf6", "Ng1", "Ng8"}) {
        a.play(*a.parseSan(text));
    }
    EXPECT_EQ(a.hash(), b.hash()); // same position, different route
    b.play(*b.parseSan("e4"));
    EXPECT_NE(a.hash(), b.hash());
}

} // namespace
