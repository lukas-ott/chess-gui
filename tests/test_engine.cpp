// The engine module: reading UCI output, scores, engine lines as moves, picking a download.

#include "kiebitz/engine/release.hpp"
#include "kiebitz/engine/score.hpp"
#include "kiebitz/engine/uci.hpp"

#include <gtest/gtest.h>

namespace {

using namespace kiebitz;
using engine::Score;

core::Position fromFen(std::string_view fen) {
    return core::Position::fromFen(fen).value();
}

TEST(UciInfo, ReadsAFullLine) {
    const auto info = engine::parseInfo(
        "info depth 24 seldepth 33 multipv 2 score cp -17 nodes 4813924 nps 1203481 hashfull 120 "
        "tbhits 0 time 4000 pv e7e5 g1f3 b8c6");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->multiPv, 2);
    EXPECT_EQ(info->depth, 24);
    EXPECT_EQ(info->selDepth, 33);
    EXPECT_EQ(info->score, (Score{Score::Kind::Centipawns, -17}));
    EXPECT_EQ(info->nodes, 4813924U);
    EXPECT_EQ(info->nps, 1203481U);
    EXPECT_EQ(info->timeMs, 4000U);
    EXPECT_EQ(info->pv, (std::vector<std::string>{"e7e5", "g1f3", "b8c6"}));
}

TEST(UciInfo, MateBoundsAndDefaults) {
    const auto mate = engine::parseInfo("info depth 30 score mate -3 pv h7h8");
    ASSERT_TRUE(mate.has_value());
    EXPECT_EQ(mate->multiPv, 1); // absent: the best line
    EXPECT_EQ(mate->score, (Score{Score::Kind::Mate, -3}));

    const auto lower = engine::parseInfo("info depth 9 score cp 40 lowerbound pv e2e4");
    ASSERT_TRUE(lower.has_value());
    EXPECT_EQ(lower->score.bound, Score::Bound::Lower);
}

TEST(UciInfo, IgnoresLinesWithoutAResult) {
    for (const char* line :
         {"info depth 20 currmove e2e4 currmovenumber 1",
          "info string NNUE evaluation using nn-1111cefa1111.nnue enabled",
          "info depth 5 score cp 10", // no line
          "info depth 5 pv e2e4",     // no score
          "info depth 5 score banana 10 pv e2e4", "bestmove e2e4", "", "readyok"}) {
        EXPECT_FALSE(engine::parseInfo(line).has_value()) << line;
    }
}

TEST(UciOption, SpinComboCheckAndSpaces) {
    const auto threads =
        engine::parseOption("option name Threads type spin default 1 min 1 max 1024");
    ASSERT_TRUE(threads.has_value());
    EXPECT_EQ(threads->name, "Threads");
    EXPECT_EQ(threads->type, "spin");
    EXPECT_EQ(threads->defaultValue, "1");
    EXPECT_EQ(threads->min, 1);
    EXPECT_EQ(threads->max, 1024);

    const auto skill = engine::parseOption("option name Skill Level type spin default 20 min 0 "
                                           "max 20");
    ASSERT_TRUE(skill.has_value());
    EXPECT_EQ(skill->name, "Skill Level");
    EXPECT_EQ(skill->max, 20);

    const auto combo = engine::parseOption(
        "option name Style type combo default Normal var Solid var Normal var Risky");
    ASSERT_TRUE(combo.has_value());
    EXPECT_EQ(combo->defaultValue, "Normal");
    EXPECT_EQ(combo->choices, (std::vector<std::string>{"Solid", "Normal", "Risky"}));

    const auto file = engine::parseOption("option name EvalFile type string default <empty>");
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->defaultValue, "");

    EXPECT_FALSE(engine::parseOption("option name type spin").has_value());
    EXPECT_FALSE(engine::parseOption("id name Stockfish").has_value());
}

TEST(UciOther, IdNameBestMoveAndCommands) {
    EXPECT_EQ(engine::parseIdName("id name Stockfish 19"), "Stockfish 19");
    EXPECT_FALSE(engine::parseIdName("id author someone").has_value());
    EXPECT_TRUE(engine::isBestMove("bestmove e2e4 ponder e7e5"));
    EXPECT_TRUE(engine::isBestMove("bestmove (none)"));
    EXPECT_FALSE(engine::isBestMove("info bestmove"));
    EXPECT_EQ(engine::setOptionCommand("Hash", "256"), "setoption name Hash value 256");
    EXPECT_EQ(engine::positionCommand(core::Position()),
              "position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

TEST(UciPv, BecomesLegalMovesAndStopsAtTheFirstBadOne) {
    const core::Position start;
    const auto moves = engine::pvMoves(start, {"e2e4", "e7e5", "g1f3", "e7e5", "b8c6"});
    ASSERT_EQ(moves.size(), 3U); // the second e7e5 isn't legal: the line ends there
    EXPECT_EQ(moves[2].uci(), "g1f3");
    // Castling written as king-takes-rook (the Lichess cloud) comes out as the king's move.
    const auto castle =
        engine::pvMoves(fromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"), {"e1h1", "e8a8"});
    ASSERT_EQ(castle.size(), 2U);
    EXPECT_EQ(castle[0].uci(), "e1g1");
    EXPECT_EQ(castle[1].uci(), "e8c8");
}

TEST(Scores, WhitesPointOfView) {
    const Score black{Score::Kind::Centipawns, 50, Score::Bound::Lower};
    const Score white = black.forWhite(core::Color::Black);
    EXPECT_EQ(white.value, -50);
    EXPECT_EQ(white.bound, Score::Bound::Upper);
    EXPECT_EQ(black.forWhite(core::Color::White), black);
}

TEST(Scores, Formatting) {
    EXPECT_EQ(engine::formatScore({Score::Kind::Centipawns, 34}), "+0.34");
    EXPECT_EQ(engine::formatScore({Score::Kind::Centipawns, -120}), "-1.20");
    EXPECT_EQ(engine::formatScore({Score::Kind::Centipawns, 0}), "0.00");
    EXPECT_EQ(engine::formatScore({Score::Kind::Centipawns, -5}), "-0.05");
    EXPECT_EQ(engine::formatScore({Score::Kind::Mate, 3}), "#3");
    EXPECT_EQ(engine::formatScore({Score::Kind::Mate, -2}), "-#2");
    EXPECT_EQ(engine::formatScore({Score::Kind::Over, 1}), "1-0");
    EXPECT_EQ(engine::formatScore({Score::Kind::Over, -1}), "0-1");
    EXPECT_EQ(engine::formatScore({Score::Kind::Over, 0}), "\u00BD-\u00BD");
}

TEST(Scores, WinningChancesForTheEvalBar) {
    EXPECT_DOUBLE_EQ(engine::winningChances({Score::Kind::Centipawns, 0}), 0.0);
    const double pawn = engine::winningChances({Score::Kind::Centipawns, 100});
    EXPECT_GT(pawn, 0.15);
    EXPECT_LT(pawn, 0.2);
    EXPECT_NEAR(engine::winningChances({Score::Kind::Centipawns, -100}), -pawn, 1e-12);
    EXPECT_LT(engine::winningChances({Score::Kind::Centipawns, 5000}), 1.0); // never all White
    EXPECT_DOUBLE_EQ(engine::winningChances({Score::Kind::Mate, 4}), 1.0);
    EXPECT_DOUBLE_EQ(engine::winningChances({Score::Kind::Mate, -1}), -1.0);
    // Checkmate on the board: all the way to the winner, whichever side that is.
    EXPECT_DOUBLE_EQ(
        engine::winningChances(Score{Score::Kind::Over, -1}.forWhite(core::Color::White)), -1.0);
    EXPECT_DOUBLE_EQ(
        engine::winningChances(Score{Score::Kind::Over, -1}.forWhite(core::Color::Black)), 1.0);
    EXPECT_DOUBLE_EQ(engine::winningChances({Score::Kind::Over, 0}), 0.0);
}

TEST(Release, PicksTheRightMacDownload) {
    using engine::Cpu;
    // Stockfish 17: one file per CPU level.
    const std::vector<std::string> sf17{
        "stockfish-android-armv8.tar",      "stockfish-macos-m1-apple-silicon.tar",
        "stockfish-macos-x86-64-avx2.tar",  "stockfish-macos-x86-64.tar",
        "stockfish-ubuntu-x86-64-avx2.tar", "stockfish-windows-x86-64-avx2.zip"};
    EXPECT_EQ(engine::pickMacDownload(sf17, Cpu::Arm64), 1U);
    EXPECT_EQ(engine::pickMacDownload(sf17, Cpu::X86_64), 3U);
    // Stockfish 19: one universal file.
    const std::vector<std::string> sf19{"stockfish-ubuntu-x86-64.tar.gz",
                                        "stockfish-macos-universal.tar.gz",
                                        "stockfish-windows-x86-64.zip"};
    EXPECT_EQ(engine::pickMacDownload(sf19, Cpu::Arm64), 1U);
    EXPECT_EQ(engine::pickMacDownload(sf19, Cpu::X86_64), 1U);
    EXPECT_FALSE(engine::pickMacDownload({"stockfish-ubuntu.tar", "source.zip"}, Cpu::Arm64));
    EXPECT_FALSE(engine::pickMacDownload({"stockfish-macos-x86-64.tar"}, Cpu::Arm64));
}

TEST(Release, Sha256FromGitHubDigest) {
    const std::string hex(64, 'a');
    EXPECT_EQ(engine::sha256FromDigest("sha256:" + hex), hex);
    EXPECT_EQ(engine::sha256FromDigest("sha256:" + std::string(64, 'A')), hex);
    EXPECT_FALSE(engine::sha256FromDigest("sha1:" + hex).has_value());
    EXPECT_FALSE(engine::sha256FromDigest("sha256:abc").has_value());
    EXPECT_FALSE(engine::sha256FromDigest("").has_value());
}

} // namespace
