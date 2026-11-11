#include "kiebitz/pgn/pgn.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using namespace kiebitz;
using core::Game;
using core::NodeId;

Game parse(std::string_view text) {
    auto game = pgn::parseGame(text);
    EXPECT_TRUE(game.ok()) << (game.ok() ? "" : game.error().message);
    return game.ok() ? std::move(*game) : Game();
}

std::vector<std::string> mainLine(const Game& game) {
    std::vector<std::string> sans;
    for (const NodeId id : game.mainLine()) {
        if (id != Game::root()) {
            sans.push_back(game.node(id).san);
        }
    }
    return sans;
}

std::vector<std::string> sansOf(const Game& game, const std::vector<NodeId>& ids) {
    std::vector<std::string> sans;
    sans.reserve(ids.size());
    for (const NodeId id : ids) {
        sans.push_back(game.node(id).san);
    }
    return sans;
}

core::Square sq(std::string_view name) {
    return core::Square::parse(name).value();
}

// ---- Reading -----------------------------------------------------------------------------------

TEST(PgnRead, HeadersAndMainLine) {
    const Game game = parse(R"([Event "Casual"]
[White "Morphy, Paul"]
[Black "Duke Karl / Count Isouard"]
[Result "1-0"]

1. e4 e5 2. Nf3 d6 3. d4 Bg4 1-0
)");
    EXPECT_EQ(game.header("Event"), "Casual");
    EXPECT_EQ(game.header("White"), "Morphy, Paul");
    EXPECT_EQ(game.header("Result"), "1-0");
    EXPECT_EQ(game.headers().size(), 4U); // exactly the file's headers, in its order
    EXPECT_EQ(game.headers()[1].first, "White");
    EXPECT_EQ(mainLine(game), (std::vector<std::string>{"e4", "e5", "Nf3", "d6", "d4", "Bg4"}));
}

TEST(PgnRead, NestedVariations) {
    const Game game = parse("1. e4 e5 (1... c5 2. Nf3 (2. c3 d5) d6) 2. Nf3 *");
    const auto main = game.mainLine();
    EXPECT_EQ(mainLine(game), (std::vector<std::string>{"e4", "e5", "Nf3"}));
    const NodeId e4 = main[1];
    EXPECT_EQ(sansOf(game, game.children(e4)), (std::vector<std::string>{"e5", "c5"}));
    const NodeId c5 = game.children(e4)[1];
    const NodeId nf3 = game.mainChild(c5);
    EXPECT_EQ(sansOf(game, game.children(c5)), (std::vector<std::string>{"Nf3", "c3"}));
    EXPECT_EQ(game.node(game.mainChild(nf3)).san, "d6");
    EXPECT_EQ(game.node(game.mainChild(game.children(c5)[1])).san, "d5");
}

TEST(PgnRead, SiblingVariations) {
    const Game game = parse("1. e4 (1. d4 d5) (1. c4) (1. Nf3 Nf6) 1... e5 *");
    EXPECT_EQ(sansOf(game, game.children(Game::root())),
              (std::vector<std::string>{"e4", "d4", "c4", "Nf3"}));
    EXPECT_EQ(mainLine(game), (std::vector<std::string>{"e4", "e5"}));
}

TEST(PgnRead, Comments) {
    const Game game = parse(R"({Game start} 1. e4 {Best by test} {[%cal Ge2e4]} e5
(; a line comment
{Sicilian:} 1... c5) 2. Nf3 *)");
    EXPECT_EQ(game.node(Game::root()).comment, "Game start");
    const NodeId e4 = game.mainLine()[1];
    EXPECT_EQ(game.node(e4).comment, "Best by test"); // two comments merged
    ASSERT_EQ(game.node(e4).arrows.size(), 1U);
    EXPECT_EQ(game.node(e4).arrows[0].from, sq("e2"));
    const NodeId c5 = game.children(e4)[1];
    EXPECT_EQ(game.node(c5).commentBefore, "a line comment Sicilian:");
}

TEST(PgnRead, NagsAndSuffixes) {
    const Game game = parse("1. e4 $1 e5?! 2. Nf3!? $14 Nc6 *");
    const auto main = game.mainLine();
    EXPECT_EQ(game.node(main[1]).nags, (std::vector<std::uint8_t>{1}));
    EXPECT_EQ(game.node(main[2]).nags, (std::vector<std::uint8_t>{6}));
    EXPECT_EQ(game.node(main[3]).nags, (std::vector<std::uint8_t>{5, 14}));
    EXPECT_EQ(game.node(main[3]).san, "Nf3"); // the suffix isn't part of the move
}

TEST(PgnRead, GluedAndListedAnnotations) {
    // Seen in real files: evaluation marks glued to the move, comma lists, repeats, junk.
    const Game game =
        parse("1. d4 Nf6 2. Bf4!,+- d5 3. e3 e6± 4. Nd2?,?,?!,?!?,?,?! c5 5. c3!,!?!,!? *");
    const auto main = game.mainLine();
    EXPECT_EQ(game.node(main[3]).san, "Bf4");
    EXPECT_EQ(game.node(main[3]).nags, (std::vector<std::uint8_t>{1, 18}));
    EXPECT_EQ(game.node(main[6]).nags, (std::vector<std::uint8_t>{16}));
    EXPECT_EQ(game.node(main[7]).nags, (std::vector<std::uint8_t>{2, 6}));
    EXPECT_EQ(game.node(main[9]).nags, (std::vector<std::uint8_t>{1, 5}));
}

TEST(PgnRead, NonStandardDraws) {
    const Game game = parse("[Result \"0.5-0.5\"]\n\n1. d4 d5 0.5-0.5");
    EXPECT_EQ(game.header("Result"), "1/2-1/2");
    EXPECT_EQ(mainLine(game).size(), 2U);
    const auto games = pgn::indexGames("1. e4 e5 0.5-0.5\n1. d4 d5 \xC2\xBD-\xC2\xBD\n");
    EXPECT_EQ(games.size(), 2U);
}

TEST(PgnRead, MessyMovetext) {
    // Glued numbers, missing numbers, "1..." after comments, zeros for castling, no "x".
    const Game game =
        parse("1.e4 e5 2.Nf3 Nc6 3.Bc4 Bc5 4.0-0 Nf6 5.d3 d6 6.Bg5 h6 7.Bf6 Qf6 8... ; tail\n *");
    EXPECT_EQ(mainLine(game),
              (std::vector<std::string>{"e4", "e5", "Nf3", "Nc6", "Bc4", "Bc5", "O-O", "Nf6", "d3",
                                        "d6", "Bg5", "h6", "Bxf6", "Qxf6"}));
}

TEST(PgnRead, EscapedTagsAndEscapeLines) {
    const Game game = parse(R"(% this line is ignored
[Event "The \"Immortal\" Game"]
[Annotator "C:\\chess"]

1. e4 *)");
    EXPECT_EQ(game.header("Event"), "The \"Immortal\" Game");
    EXPECT_EQ(game.header("Annotator"), "C:\\chess");
}

TEST(PgnRead, GameFromFen) {
    const Game game = parse(R"([SetUp "1"]
[FEN "4k3/8/8/8/8/8/4P3/4K3 b - - 0 30"]

30... Kd7 31. e4 *)");
    EXPECT_EQ(game.startPosition().fen(), "4k3/8/8/8/8/8/4P3/4K3 b - - 0 30");
    EXPECT_EQ(mainLine(game), (std::vector<std::string>{"Kd7", "e4"}));
}

TEST(PgnRead, NoHeadersStillMakesAGame) {
    const Game game = parse("1. d4 d5 2. c4 *");
    EXPECT_EQ(game.header("Event"), "?"); // the defaults
    EXPECT_EQ(mainLine(game).size(), 3U);
}

TEST(PgnRead, StopsAtTheResult) {
    const Game game = parse("1. e4 e5 1-0 2. Nf3 Nc6");
    EXPECT_EQ(mainLine(game).size(), 2U);
}

struct BadPgn {
    const char* name;
    const char* text;
    const char* messagePart;
};

class PgnErrors : public ::testing::TestWithParam<BadPgn> {};

TEST_P(PgnErrors, HaveClearMessages) {
    const auto game = pgn::parseGame(GetParam().text);
    ASSERT_FALSE(game.ok());
    EXPECT_NE(game.error().message.find(GetParam().messagePart), std::string::npos)
        << "message was: " << game.error().message;
}

INSTANTIATE_TEST_SUITE_P(
    PgnRead, PgnErrors,
    ::testing::Values(BadPgn{"IllegalMove", "1. e4 e5\n2. Nf3 Nc6\n3. Bb5 Ke6 *",
                             "Line 3: move 3... \"Ke6\" is not a legal move here."},
                      BadPgn{"UnclosedVariation", "1. e4 (1. d4 d5 *", "never closed"},
                      BadPgn{"UnmatchedClose", "1. e4 ) e5 *", "never opened"},
                      BadPgn{"VariationFirst", "( 1. e4 ) *", "must follow the move"},
                      BadPgn{"BadFen", "[FEN \"8/8/8 w - -\"]\n\n*", "FEN header is invalid"},
                      BadPgn{"NullMove", "1. e4 -- 2. d4 *", "null moves"}),
    [](const auto& testCase) { return std::string(testCase.param.name); });

// ---- Index -------------------------------------------------------------------------------------

constexpr std::string_view kTwoGames = R"([Event "One"]
[White "A"]

1. e4 e5 {a 1-0 inside} 2. Nf3 (2. f4 1-0) 2... Nc6 1-0

[Event "Two"]
[White "B"]

1. d4 d5 *
)";

TEST(PgnIndex, FindsGamesAndTheirHeaders) {
    const auto games = pgn::indexGames(kTwoGames);
    ASSERT_EQ(games.size(), 2U);
    EXPECT_EQ(games[0].header("Event"), "One");
    EXPECT_EQ(games[1].header("White"), "B");
    // Results inside comments and variations don't end the game.
    EXPECT_TRUE(kTwoGames.substr(games[0].offset, games[0].length).ends_with("2... Nc6 1-0"));
    EXPECT_TRUE(kTwoGames.substr(games[1].offset).starts_with("[Event \"Two\"]"));
    // Each entry parses on its own.
    for (const auto& entry : games) {
        EXPECT_TRUE(pgn::parseGame(kTwoGames.substr(entry.offset, entry.length)).ok());
    }
}

TEST(PgnIndex, GamesWithoutResultsOrHeaders) {
    const auto games = pgn::indexGames("1. e4 e5 *\n1. d4 d5 *\n[Event \"X\"]\n\n1. c4");
    ASSERT_EQ(games.size(), 3U);
    EXPECT_TRUE(games[0].headers.empty());
    EXPECT_EQ(games[2].header("Event"), "X");
}

TEST(PgnIndex, ByteOrderMarkIsSkipped) {
    const std::string text = "\xEF\xBB\xBF[Event \"BOM\"]\n\n1. e4 *";
    const auto games = pgn::indexGames(text);
    ASSERT_EQ(games.size(), 1U);
    EXPECT_EQ(games[0].header("Event"), "BOM");
    EXPECT_TRUE(pgn::parseGame(text).ok());
}

TEST(PgnIndex, EmptyText) {
    EXPECT_TRUE(pgn::indexGames("").empty());
    EXPECT_TRUE(pgn::indexGames("   \n\n").empty());
}

TEST(PgnIndex, ParseAllReportsWhichGameFailed) {
    const auto games = pgn::parseAll("1. e4 *\n\n1. e4 e4 *");
    ASSERT_FALSE(games.ok());
    EXPECT_TRUE(games.error().message.starts_with("Game 2: Line 1: move 1... "));
}

// ---- Writing -----------------------------------------------------------------------------------

TEST(PgnWrite, StandardLayout) {
    const Game game = parse(R"([White "A"]
[Black "B"]
[Result "1-0"]
[ECO "C20"]

1. e4 {Best by test} e5 (1... c5 $1 {Sicilian} 2. Nf3) 2. Nf3 Nc6 $6 1-0)");
    EXPECT_EQ(pgn::writeGame(game), R"([Event "?"]
[Site "?"]
[Date "????.??.??"]
[Round "?"]
[White "A"]
[Black "B"]
[Result "1-0"]
[ECO "C20"]

1. e4 {Best by test} 1... e5 (1... c5 $1 {Sicilian} 2. Nf3) 2. Nf3 Nc6 $6 1-0
)");
}

TEST(PgnWrite, WrapsAt80Columns) {
    Game game;
    const std::vector<std::string_view> moves{"e4",  "e5",  "Nf3", "Nc6", "Bb5", "a6",  "Ba4",
                                              "Nf6", "O-O", "Be7", "Re1", "b5",  "Bb3", "d6",
                                              "c3",  "O-O", "h3",  "Nb8", "d4",  "Nbd7"};
    ASSERT_TRUE(game.addLine(Game::root(), moves).ok());
    const std::string text = pgn::writeGame(game);
    const auto movetext = text.substr(text.find("\n\n") + 2);
    std::size_t start = 0;
    while (start < movetext.size()) {
        const auto end = movetext.find('\n', start);
        EXPECT_LE(end - start, 80U);
        start = end + 1;
    }
    EXPECT_NE(movetext.find('\n'), movetext.size() - 1); // more than one line
}

TEST(PgnWrite, DrawingsAndEscapes) {
    Game game;
    const NodeId e4 = game.addSan(Game::root(), "e4").value();
    game.setArrows(e4, {{sq("g1"), sq("f3"), core::MarkColor::Green}});
    game.setSquareMarks(e4, {{sq("d5"), core::MarkColor::Red}});
    game.setComment(e4, "Aims at d5 {sic}");
    game.setHeader("Event", "The \"Big\" One");
    const std::string text = pgn::writeGame(game);
    EXPECT_NE(text.find(R"([Event "The \"Big\" One"])"), std::string::npos);
    EXPECT_NE(text.find("1. e4 {[%csl Rd5] [%cal Gg1f3] Aims at d5 {sic)} *"), std::string::npos);
}

TEST(PgnWrite, RoundTripKeepsEverything) {
    constexpr std::string_view kRich = R"([Event "Round trip"]
[White "W"]
[Black "B"]
[Result "*"]
[Annotator "Test"]

{Start} 1. e4 $1 {[%cal Ge2e4] Centre} 1... e5 (1... c5 {Sicilian} 2. Nf3 (2. c3 d5)
2... d6) (1... e6 $6) 2. Nf3 Nc6 3. Bb5 {[%csl Rc6] Pins nothing yet} 3... a6 *
)";
    const Game first = parse(kRich);
    const std::string written = pgn::writeGame(first);
    const Game second = parse(written);
    EXPECT_EQ(pgn::writeGame(second), written); // stable
    EXPECT_EQ(second.nodeCount(), first.nodeCount());
    EXPECT_EQ(second.node(Game::root()).comment, "Start");
    const NodeId bb5 = second.mainLine()[5];
    EXPECT_EQ(second.node(bb5).squareMarks.size(), 1U);
    EXPECT_EQ(second.node(bb5).comment, "Pins nothing yet");
    EXPECT_EQ(second.header("Annotator"), "Test");
}

TEST(PgnWrite, NeverBreaksInsideADrawingCommand) {
    Game game;
    const NodeId e4 = game.addSan(Game::root(), "e4").value();
    game.setComment(e4, std::string(60, 'x')); // pushes the drawings towards the line end
    game.setArrows(e4, {{sq("g1"), sq("f3"), core::MarkColor::Green},
                        {sq("b1"), sq("c3"), core::MarkColor::Red}});
    game.setSquareMarks(e4, {{sq("d5"), core::MarkColor::Red}});
    const std::string text = pgn::writeGame(game);
    EXPECT_NE(text.find("[%cal Gg1f3,Rb1c3]"), std::string::npos) << text;
    EXPECT_NE(text.find("[%csl Rd5]"), std::string::npos) << text;
}

TEST(PgnWrite, FenGamesKeepTheirStart) {
    const Game game = parse(R"([SetUp "1"]
[FEN "4k3/8/8/8/8/8/4P3/4K3 b - - 0 30"]

30... Kd7 *)");
    const Game again = parse(pgn::writeGame(game));
    EXPECT_EQ(again.startPosition().fen(), "4k3/8/8/8/8/8/4P3/4K3 b - - 0 30");
    EXPECT_NE(pgn::writeGame(game).find("30... Kd7 *"), std::string::npos);
}

} // namespace

TEST(PgnEdit, SetsAHeaderInEveryGameAndKeepsTheRest) {
    const std::string text = "[Event \"A\"]\n[Result \"*\"]\n\n1. e4  {keep   spacing} *\n\n"
                             "[Event \"B\"]\n[Repertoire \"Black\"]\n[Result \"*\"]\n\n1. d4 *\n";
    const std::string marked = pgn::setHeaderInAllGames(text, "Repertoire", "White");
    EXPECT_EQ(marked, "[Event \"A\"]\n[Result \"*\"]\n[Repertoire \"White\"]\n\n1. e4  {keep   "
                      "spacing} *\n\n[Event \"B\"]\n[Repertoire \"White\"]\n[Result \"*\"]\n\n1. "
                      "d4 *\n");
    const auto games = pgn::indexGames(marked);
    ASSERT_EQ(games.size(), 2U);
    EXPECT_EQ(games[0].header("Repertoire"), "White");
    EXPECT_EQ(games[1].header("Repertoire"), "White");

    // Removing it gives back the first game exactly, and the second without the line.
    const std::string unmarked = pgn::setHeaderInAllGames(marked, "Repertoire", std::nullopt);
    EXPECT_EQ(unmarked, "[Event \"A\"]\n[Result \"*\"]\n\n1. e4  {keep   spacing} *\n\n"
                        "[Event \"B\"]\n[Result \"*\"]\n\n1. d4 *\n");
}

TEST(PgnEdit, GameWithoutHeadersGetsOne) {
    const std::string marked = pgn::setHeaderInAllGames("1. e4 e5 *\n", "Repertoire", "Black");
    EXPECT_EQ(marked, "[Repertoire \"Black\"]\n\n1. e4 e5 *\n");
    EXPECT_EQ(pgn::indexGames(marked).front().header("Repertoire"), "Black");
}
