#include "kiebitz/pgn/pgn.hpp"
#include "kiebitz/puzzles/history.hpp"
#include "kiebitz/puzzles/puzzle.hpp"
#include "kiebitz/puzzles/store.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <zstd.h>

namespace core = kiebitz::core;
namespace puzzles = kiebitz::puzzles;

namespace {

// Three lines of the Lichess puzzle database, as published.
constexpr const char* kCsv =
    "PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags\n"
    "00008,r6k/pp2r2p/4Rp1Q/3p4/8/1N1P2R1/PqP2bPP/7K b - - 0 24,f2g3 e6e7 b2b1 b3c1 b1c1 h6c1,"
    "1819,77,95,10446,crushing hangingPiece long middlegame,"
    "https://lichess.org/game/787zsVup/black#48,\n"
    "0000D,5rk1/1p3ppp/pq3b2/8/8/1P1Q1N2/P4PPP/3R2K1 w - - 2 27,d3d6 f8d8 d6d8 f6d8,1433,75,96,"
    "37700,advantage endgame short,https://lichess.org/game/F8M8OS71#53,\n"
    "0000H,r4rk1/pp3ppp/8/2bqp2b/3N4/P2Bn2P/1PPN1P2/R1BQ1RK1 w - - 1 15,d1h5 d5g2,1032,78,100,92,"
    "mate mateIn1 middlegame oneMove,https://lichess.org/game/EwHRkfas#29,French_Defense\n";

core::Move uci(std::string_view text) {
    return core::Move::fromUci(text).value();
}

std::string tempPath(const char* name) {
    return (std::filesystem::temp_directory_path() /
            (std::string("kiebitz_") + name + "_" + std::to_string(::getpid())))
        .string();
}

/// Feeds a string to an import, a little at a time (as a download would come).
struct Reader {
    std::string data;
    std::size_t at = 0;
    std::size_t operator()(char* buffer, std::size_t size) {
        const std::size_t n = std::min({size, data.size() - at, std::size_t{7000}});
        std::copy_n(data.data() + at, n, buffer);
        at += n;
        return n;
    }
};

} // namespace

TEST(Puzzles, ALichessLineIsReadWithTheOpponentsMoveFirst) {
    const auto puzzle = puzzles::parseLichessLine(
        "00008,r6k/pp2r2p/4Rp1Q/3p4/8/1N1P2R1/PqP2bPP/7K b - - 0 24,f2g3 e6e7 b2b1 b3c1 b1c1 "
        "h6c1,1819,77,95,10446,crushing hangingPiece long middlegame,https://x,");
    ASSERT_TRUE(puzzle.has_value());
    EXPECT_EQ(puzzle->id, "00008");
    EXPECT_EQ(puzzle->rating, 1819);
    EXPECT_TRUE(puzzle->opponentFirst);
    EXPECT_EQ(puzzle->themes,
              (std::vector<std::string>{"crushing", "hangingPiece", "long", "middlegame"}));
    EXPECT_EQ(puzzle->game.mainLine().size(), 7U); // the root and six moves
    EXPECT_FALSE(puzzles::parseLichessLine("PuzzleId,FEN,Moves,Rating,RatingDeviation,"
                                           "Popularity,NbPlays,Themes,GameUrl,OpeningTags")
                     .has_value());
    EXPECT_FALSE(puzzles::parseLichessLine("x,not a fen,e2e4 e7e5,1500,0,0,0,fork,,").has_value());
}

TEST(Puzzles, SolvingTakesTheRightMovesAndTheOpponentAnswers) {
    const auto puzzle = puzzles::parseLichessLine(
        "00008,r6k/pp2r2p/4Rp1Q/3p4/8/1N1P2R1/PqP2bPP/7K b - - 0 24,f2g3 e6e7 b2b1 b3c1 b1c1 "
        "h6c1,1819,77,95,10446,crushing,https://x,");
    puzzles::Solve solve(*puzzle);
    // Black played Bxg3; White solves.
    EXPECT_EQ(solve.solver(), core::Color::White);
    EXPECT_EQ(solve.node(), solve.start());
    EXPECT_EQ(solve.expected(), uci("e6e7"));
    // A wrong move: nothing is played, and it counts as failed.
    EXPECT_EQ(solve.play(uci("h6h7")), puzzles::Solve::Verdict::Wrong);
    EXPECT_EQ(solve.node(), solve.start());
    EXPECT_TRUE(solve.failed());
    EXPECT_EQ(solve.play(uci("e6e7")), puzzles::Solve::Verdict::Right);
    EXPECT_TRUE(solve.awaitingReply());
    EXPECT_EQ(solve.play(uci("b3c1")), puzzles::Solve::Verdict::Wrong); // not before the reply
    EXPECT_EQ(solve.game().node(solve.reply()).move, uci("b2b1"));
    EXPECT_EQ(solve.play(uci("b3c1")), puzzles::Solve::Verdict::Right);
    solve.reply();
    EXPECT_EQ(solve.play(uci("h6c1")), puzzles::Solve::Verdict::Solved);
    EXPECT_TRUE(solve.solved());
    EXPECT_FALSE(solve.expected().has_value());
}

TEST(Puzzles, AnotherMateSolvesItToo) {
    // A back-rank mate: the puzzle has Rc8#, Rd8# mates as well.
    puzzles::Puzzle puzzle;
    puzzle.game =
        core::Game(core::Position::fromFen("6k1/5ppp/p7/8/8/8/8/2RR2K1 b - - 0 1").value());
    puzzle.opponentFirst = true;
    const core::NodeId first = puzzle.game.addMove(core::Game::root(), uci("a6a5"));
    puzzle.game.addMove(first, uci("c1c8"));
    puzzles::Solve solve(puzzle);
    EXPECT_EQ(solve.play(uci("d1d8")), puzzles::Solve::Verdict::Solved);
    EXPECT_FALSE(solve.failed());
}

TEST(Puzzles, APuzzleFileGameIsSolvedAlongItsMainLine) {
    // From a puzzle file: Black to move, the main line is the solution, the variation another
    // answer of White's that the file notes.
    const auto games = kiebitz::pgn::parseAll(
        "[SetUp \"1\"]\n[FEN \"5rk1/3nppbp/3p2p1/qNpP4/Pr2P3/6P1/1P3PKP/R1B1Q2R b - - 0 1\"]\n\n"
        "{Schwarz am Zug!} 1... Rxb5 $1 2. axb5 (2. Qxa5 Rxa5) 2... Qxa1 *\n\n"
        "[SetUp \"1\"]\n[FEN \"5rk1/3nppbp/3p2p1/qNpP4/Pr2P3/6P1/1P3PKP/R1B1Q2R b - - 0 1\"]\n\n"
        "{A task without its solution} *\n");
    ASSERT_TRUE(games.ok());
    ASSERT_EQ(games->size(), 2U);
    const auto puzzle = puzzles::fromGame((*games)[0]);
    ASSERT_TRUE(puzzle.has_value());
    EXPECT_FALSE(puzzles::fromGame((*games)[1]).has_value());
    puzzles::Solve solve(*puzzle);
    EXPECT_EQ(solve.solver(), core::Color::Black);
    const core::Position start = solve.position();
    EXPECT_EQ(solve.play(start.parseSan("Rxb5").value()), puzzles::Solve::Verdict::Right);
    solve.reply();
    EXPECT_EQ(solve.play(solve.position().parseSan("Qxa1").value()),
              puzzles::Solve::Verdict::Solved);
}

TEST(Puzzles, ThemesReadWell) {
    EXPECT_EQ(puzzles::themeName("backRankMate"), "Back rank mate");
    EXPECT_EQ(puzzles::themeName("fork"), "Fork");
    EXPECT_EQ(puzzles::themeName("mateIn2"), "Mate in 2");
}

TEST(PuzzleDb, ImportedPuzzlesArePickedByRatingAndTheme) {
    const std::string path = tempPath("puzzles");
    // zstd-compressed, as Lichess publishes it.
    std::string compressed(ZSTD_compressBound(std::strlen(kCsv)), '\0');
    compressed.resize(
        ZSTD_compress(compressed.data(), compressed.size(), kCsv, std::strlen(kCsv), 3));
    // Lichess's file begins with a skippable frame (its frame sizes, for parallel reading).
    const std::string skippable(std::string("\x50\x2A\x4D\x18\x04\x00\x00\x00", 8) + "abcd");
    Reader reader{skippable + compressed};
    const auto result = puzzles::importLichess(std::ref(reader), path);
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.puzzles, 3U);
    EXPECT_EQ(result.skipped, 0U);

    const auto db = puzzles::PuzzleDb::open(path);
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(db->size(), 3U);
    EXPECT_EQ(db->ratingRange(), (std::pair{1032, 1819}));
    EXPECT_EQ(db->count(1000, 1500), 2U);
    // Sorted by rating: 0000H (1032), 0000D (1433), 00008 (1819).
    const auto easiest = db->puzzle(0);
    ASSERT_TRUE(easiest.has_value());
    EXPECT_EQ(easiest->id, "0000H");
    EXPECT_EQ(easiest->game.startPosition().fen(),
              "r4rk1/pp3ppp/8/2bqp2b/3N4/P2Bn2P/1PPN1P2/R1BQ1RK1 w - - 0 15");
    EXPECT_EQ(easiest->themes,
              (std::vector<std::string>{"mate", "mateIn1", "middlegame", "oneMove"}));
    EXPECT_EQ(db->find("00008"), 2U);

    std::mt19937_64 random(1); // NOLINT(bugprone-random-generator-seed): the same picks each run
    for (int i = 0; i < 20; ++i) {
        const auto index = db->pick(1400, 2000, -1, random);
        ASSERT_TRUE(index.has_value());
        EXPECT_GE(db->rating(*index), 1400);
    }
    const auto& themes = db->themes();
    const auto mateIn1 = std::find(themes.begin(), themes.end(), "mateIn1") - themes.begin();
    EXPECT_EQ(db->pick(0, 3000, static_cast<int>(mateIn1), random), 0U);
    EXPECT_FALSE(db->pick(1500, 1600, -1, random).has_value());
    EXPECT_FALSE(db->pick(1400, 3000, static_cast<int>(mateIn1), random).has_value());
    std::remove(path.c_str());
}

TEST(PuzzleDb, BrokenOrCancelledImportsWriteNothing) {
    const std::string path = tempPath("broken");
    Reader damaged{std::string("\x28\xB5\x2F\xFD garbage that isn't zstd")};
    EXPECT_FALSE(puzzles::importLichess(std::ref(damaged), path).error.empty());
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_FALSE(std::filesystem::exists(path + ".part"));
    EXPECT_FALSE(std::filesystem::exists(path + ".records"));

    const std::atomic<bool> stop{true};
    Reader plain{kCsv};
    puzzles::PuzzleImportOptions options;
    options.cancel = &stop;
    EXPECT_TRUE(puzzles::importLichess(std::ref(plain), path, options).cancelled);
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_EQ(puzzles::PuzzleDb::open(path), nullptr);
}

TEST(PuzzleHistory, AttemptsAddUpToStreaksAndAccuracy) {
    const std::string path = tempPath("history");
    std::remove(path.c_str());
    auto opened = puzzles::History::open(path);
    ASSERT_TRUE(opened.ok()) << opened.error().message;
    auto& history = **opened;
    const auto attempt = [&](const char* puzzle, bool clean, std::int64_t millis) {
        history.record({"lichess", puzzle, true, clean, millis, 1500, 0});
    };
    attempt("a", true, 10000);
    attempt("b", true, 20000);
    attempt("c", false, 30000);
    attempt("d", true, 40000);
    // Solved with the clock stopped: counted, but not in the average time.
    history.record({"other", "x", true, true, -1, 0, 0});
    history.record({"other", "y", true, true, 4000, 0, 0});
    EXPECT_EQ(history.totals("other").averageMillis, 4000);
    EXPECT_EQ(history.totals("other").solved, 2);
    history.record({"file.pgn", "k1", true, false, 5000, 0, 0});
    history.record({"file.pgn", "k1", true, true, 3000, 0, 0});
    const puzzles::Totals totals = history.totals("lichess");
    EXPECT_EQ(totals.attempts, 4);
    EXPECT_EQ(totals.solved, 4);
    EXPECT_EQ(totals.clean, 3);
    EXPECT_EQ(totals.streak, 1);
    EXPECT_EQ(totals.bestStreak, 2);
    EXPECT_EQ(totals.averageMillis, 25000);
    const auto recent = history.recent("lichess", 2);
    ASSERT_EQ(recent.size(), 2U);
    EXPECT_EQ(recent[0].puzzle, "d");
    EXPECT_EQ(recent[1].puzzle, "c");
    EXPECT_FALSE(recent[1].clean);
    // A file's puzzles: the latest result of each.
    EXPECT_EQ(history.latest("file.pgn"), (std::map<std::string, bool>{{"k1", true}}));
    history.clear("file.pgn");
    EXPECT_EQ(history.totals("file.pgn").attempts, 0);
    std::remove(path.c_str());
}
