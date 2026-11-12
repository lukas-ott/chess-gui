#include "kiebitz/pgn/pgn.hpp"
#include "kiebitz/repertoire/drill.hpp"
#include "kiebitz/repertoire/gaps.hpp"
#include "kiebitz/repertoire/openings.hpp"
#include "kiebitz/repertoire/summary.hpp"
#include "kiebitz/repertoire/tree.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using namespace kiebitz;
using repertoire::Tree;

core::Game game(std::string_view pgn) {
    auto parsed = pgn::parseGame(pgn);
    EXPECT_TRUE(parsed.ok()) << (parsed.ok() ? "" : parsed.error().message);
    return parsed.ok() ? *parsed : core::Game();
}

/// The position after some SAN moves from the start.
core::Position after(std::initializer_list<std::string_view> sans) {
    core::Position position;
    for (const auto san : sans) {
        position.play(position.parseSan(san).value());
    }
    return position;
}

std::vector<core::Move> line(std::initializer_list<std::string_view> sans) {
    core::Position position;
    std::vector<core::Move> moves;
    for (const auto san : sans) {
        moves.push_back(position.parseSan(san).value());
        position.play(moves.back());
    }
    return moves;
}

std::vector<std::string> sans(const repertoire::Entry& entry) {
    std::vector<std::string> result;
    result.reserve(entry.moves.size());
    for (const auto& c : entry.moves) {
        result.push_back(c.san);
    }
    return result;
}

} // namespace

TEST(RepertoireTree, MainLineFirstThenVariations) {
    Tree tree(core::Color::White);
    tree.add(game("1. e4 (1. d4 d5) (1. c4) e5 2. Nf3 *"), 0, 0);
    const auto* start = tree.find(core::Position().hash());
    ASSERT_NE(start, nullptr);
    EXPECT_EQ(sans(*start), (std::vector<std::string>{"e4", "d4", "c4"}));
    EXPECT_TRUE(tree.isOurMove(*start));
}

TEST(RepertoireTree, TranspositionsMeetInOnePosition) {
    Tree tree(core::Color::White);
    tree.add(game("1. d4 Nf6 2. c4 e6 3. Nc3 *"), 0, 0);
    tree.add(game("1. c4 e6 2. d4 Nf6 3. Nf3 *"), 1, 0);
    const auto* entry = tree.find(after({"d4", "Nf6", "c4", "e6"}).hash());
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->sources.size(), 2U); // reached in both files
    EXPECT_EQ(sans(*entry), (std::vector<std::string>{"Nc3", "Nf3"}));
    EXPECT_EQ(entry->ply, 4);
}

TEST(RepertoireTree, FollowSaysWhereALineLeavesTheBook) {
    Tree tree(core::Color::Black);
    tree.add(game("1. e4 c5 2. Nf3 d6 (2... Nc6) 3. d4 *"), 0, 0);

    // In book all the way.
    auto book = tree.follow(core::Position(), line({"e4", "c5", "Nf3", "d6"}));
    EXPECT_EQ(book.plies, 4);
    EXPECT_FALSE(book.left);

    // They play something the repertoire doesn't cover: their deviation.
    book = tree.follow(core::Position(), line({"e4", "c5", "Nc3"}));
    EXPECT_EQ(book.plies, 2);
    EXPECT_TRUE(book.left);
    EXPECT_FALSE(book.byUs);

    // We play something else than prepared: ours, and the book says what it expected.
    book = tree.follow(core::Position(), line({"e4", "e5"}));
    EXPECT_EQ(book.plies, 1);
    EXPECT_TRUE(book.byUs);
    ASSERT_EQ(book.expected.size(), 1U);
    EXPECT_EQ(book.expected.front().san, "c5");
}

TEST(RepertoireTree, StatsFindConflictsAndOpenEnds) {
    Tree tree(core::Color::White);
    // Two chapters answer 1... c5 differently (a conflict); 2. c3 in a side variation is only an
    // alternative. 1... e5 2. Nf3 Nc6 stops on our move (an open end).
    tree.add(game("1. e4 c5 2. Nf3 (2. Nc3) 1-0"), 0, 0);
    tree.add(game("1. e4 c5 2. c3 *"), 1, 0);
    tree.add(game("1. e4 e5 2. Nf3 Nc6 *"), 0, 1);
    const auto stats = tree.stats();
    EXPECT_EQ(stats.conflicts, 1U);
    EXPECT_EQ(stats.openEnds, 1U);
    EXPECT_EQ(stats.depth, 2);
    ASSERT_EQ(tree.conflicts().size(), 1U);
    EXPECT_EQ(tree.conflicts().front(), after({"e4", "c5"}).hash());
    ASSERT_EQ(tree.openEnds().size(), 1U);
    EXPECT_EQ(tree.openEnds().front(), after({"e4", "e5", "Nf3", "Nc6"}).hash());
}

TEST(RepertoireTree, ReachCountsEachPositionOnce) {
    Tree tree(core::Color::White);
    tree.add(game("1. d4 Nf6 2. c4 e6 *"), 0, 0);
    tree.add(game("1. c4 e6 2. d4 Nf6 *"), 0, 1);
    // From the start: d4, d4 Nf6, d4 Nf6 c4, c4, c4 e6, c4 e6 d4, and the position both reach,
    // counted once.
    EXPECT_EQ(tree.reach(core::Position().hash()), 7U);
}

TEST(RepertoireTree, SideVariationsOfOursAreAlternatives) {
    Tree tree(core::Color::White);
    tree.add(game("1. e4 e5 2. Nf3 (2. Bc4 { also good }) *"), 0, 0);
    const auto* entry = tree.find(after({"e4", "e5"}).hash());
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->moves.size(), 2U);
    EXPECT_TRUE(entry->moves[0].main);
    EXPECT_FALSE(entry->moves[1].main);
    EXPECT_EQ(tree.stats().conflicts, 0U);
    // Following an alternative stays in book.
    EXPECT_FALSE(tree.follow(core::Position(), line({"e4", "e5", "Bc4"})).left);
}

TEST(RepertoireTree, CommentsComeAlong) {
    Tree tree(core::Color::White);
    tree.add(game("1. e4 { Best by test } e5 *"), 0, 0);
    const auto* start = tree.find(core::Position().hash());
    ASSERT_NE(start, nullptr);
    EXPECT_EQ(start->moves.front().comment, "Best by test");
}

TEST(OpeningNames, ReadsTheListAndFindsTranspositions) {
    const auto names = repertoire::OpeningNames::fromTsv(
        "eco\tname\tpgn\n"
        "B90\tSicilian Defense: Najdorf Variation\t1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 "
        "5. Nc3 a6\n"
        "D06\tQueen's Gambit\t1. d4 d5 2. c4\n"
        "X99\tBroken\t1. e4 e4\n");
    EXPECT_EQ(names.size(), 2U);
    const auto* qg = names.find(after({"c4", "d5", "d4"}).hash()); // a transposition
    ASSERT_NE(qg, nullptr);
    EXPECT_EQ(qg->name, "Queen's Gambit");
    // After the named moves, a game is still "in" that opening.
    const auto* along = names.along(core::Position(), line({"d4", "d5", "c4", "e6", "Nc3"}));
    ASSERT_NE(along, nullptr);
    EXPECT_EQ(along->eco, "D06");
    EXPECT_EQ(names.along(core::Position(), line({"e4"})), nullptr);
}

TEST(OpeningNames, TheBundledListParses) {
    const std::ifstream file(KIEBITZ_OPENINGS_TSV);
    ASSERT_TRUE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    const auto names = repertoire::OpeningNames::fromTsv(text.str());
    EXPECT_GT(names.size(), 3000U); // ~3,800 lines, a few name the same position
    const auto* najdorf = names.find(
        after({"e4", "c5", "Nf3", "d6", "d4", "cxd4", "Nxd4", "Nf6", "Nc3", "a6"}).hash());
    ASSERT_NE(najdorf, nullptr);
    EXPECT_EQ(najdorf->eco, "B90");
}

namespace {

core::Move moveIn(const core::Game& game, core::NodeId at, std::string_view san) {
    return game.positionAt(at).parseSan(san).value();
}

/// Plays the drill to the end, always answering right; returns the lines in the order played.
std::vector<std::string> playThrough(repertoire::Drill& drill, const core::Game& game) {
    std::vector<std::string> lines;
    while (!drill.finished()) {
        std::string line;
        while (!drill.lineEnded()) {
            if (drill.yourMove()) {
                const core::NodeId main = game.mainChild(drill.current());
                EXPECT_TRUE(drill.play(game.node(main).move).correct);
            } else {
                drill.opponentMove();
            }
            line += game.node(drill.current()).san + " ";
        }
        lines.push_back(line);
        drill.nextLine();
    }
    return lines;
}

} // namespace

TEST(Drill, EveryLineOnceThenFinished) {
    // White: 1. e4, and against 1... e5 / 1... c5 the repertoire's answers.
    const core::Game g = game("1. e4 e5 (1... c5 2. Nf3 d6 3. d4) 2. Nf3 Nc6 3. Bb5 *");
    repertoire::Drill drill(g, core::Color::White, core::Game::root(), 0, 42);
    EXPECT_EQ(drill.lines(), 2U);
    EXPECT_EQ(drill.trainable(), 5U); // 1. e4, 2. Nf3 (twice), 3. Bb5, 3. d4
    auto lines = playThrough(drill, g);
    std::sort(lines.begin(), lines.end());
    EXPECT_EQ(lines, (std::vector<std::string>{"e4 c5 Nf3 d6 d4 ", "e4 e5 Nf3 Nc6 Bb5 "}));
    EXPECT_EQ(drill.correct(), 6); // 1. e4 and 2. Nf3 are asked in both lines
    EXPECT_EQ(drill.mistakes(), 0);
}

TEST(Drill, AWrongMoveIsAMistakeOnceAndMustBeCorrected) {
    const core::Game g = game("1. e4 e5 2. Nf3 *");
    repertoire::Drill drill(g, core::Color::White, core::Game::root(), 0, 1);
    const auto wrong = drill.play(moveIn(g, core::Game::root(), "d4"));
    EXPECT_FALSE(wrong.correct);
    EXPECT_EQ(g.positionAt(core::Game::root()).san(wrong.expected), "e4");
    drill.play(moveIn(g, core::Game::root(), "c4")); // another wrong try: still one mistake
    EXPECT_EQ(drill.mistakes(), 1);
    EXPECT_EQ(drill.current(), core::Game::root()); // still there
    EXPECT_TRUE(drill.play(wrong.expected).correct);
    EXPECT_EQ(drill.correct(), 0); // got it right only after a mistake
    ASSERT_EQ(drill.missed().size(), 1U);
    EXPECT_EQ(drill.missed().front(), core::Game::root());
}

TEST(Drill, AlternativesAreNotAskedAndDepthCutsLines) {
    // Black: 1... c5 is the move; 1... e5 in a side variation is only an alternative.
    const core::Game g = game("1. e4 c5 (1... e5 2. Nf3) 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 *");
    const repertoire::Drill all(g, core::Color::Black, core::Game::root(), 0, 3);
    EXPECT_EQ(all.lines(), 1U);
    EXPECT_EQ(all.trainable(), 4U); // c5, d6, cxd4, Nf6
    const repertoire::Drill shallow(g, core::Color::Black, core::Game::root(), 2, 3);
    EXPECT_EQ(shallow.trainable(), 2U); // up to move 2: c5, d6
}

TEST(Drill, FromABranch) {
    const core::Game g = game("1. e4 e5 (1... c5 2. Nf3 d6 3. d4) 2. Nf3 Nc6 3. Bb5 *");
    const core::NodeId afterC5 = g.children(g.mainChild(core::Game::root()))[1];
    repertoire::Drill drill(g, core::Color::White, afterC5, 0, 7);
    EXPECT_EQ(drill.lines(), 1U);
    EXPECT_EQ(drill.trainable(), 2U); // 2. Nf3, 3. d4
    EXPECT_EQ(playThrough(drill, g), std::vector<std::string>{"Nf3 d6 d4 "});
}

TEST(Summary, CountsWhatPracticeAsksAndNamesTheOpenings) {
    const auto names = repertoire::OpeningNames::fromTsv("eco\tname\tpgn\n"
                                                         "C20\tKing's Pawn Game\t1. e4 e5\n"
                                                         "B20\tSicilian Defense\t1. e4 c5\n");
    // The same as Drill.EveryLineOnceThenFinished, plus a side variation of ours (2. Bc4) that
    // practice doesn't ask.
    const std::vector<core::Game> games{
        game("1. e4 e5 (1... c5 2. Nf3 d6 3. d4) 2. Nf3 (2. Bc4) Nc6 3. Bb5 *")};
    const repertoire::Summary summary = repertoire::summarize(games, core::Color::White, &names);
    EXPECT_EQ(summary.positions, 5U);
    EXPECT_EQ(summary.lines, 2U);
    EXPECT_EQ(summary.deepestMove, 3);
    ASSERT_EQ(summary.openings.size(), 2U);
    EXPECT_EQ(summary.openings[0].name, "King's Pawn Game"); // Nf3, Bb5
    EXPECT_EQ(summary.openings[0].positions, 2U);
    EXPECT_EQ(summary.openings[1].name, "Sicilian Defense"); // Nf3, d4
    EXPECT_EQ(summary.openings[1].positions, 2U);
    // No names: still the counts.
    EXPECT_TRUE(repertoire::summarize(games, core::Color::White, nullptr).openings.empty());
}

TEST(Drill, ADepthCommandCutsTheLinesBelowIt) {
    // [%depth 2] on 1... c5: the Sicilian lines stop at move 2; 1... e5 goes on.
    const core::Game g = game(
        "1. e4 e5 (1... c5 { [%depth 2] } 2. Nf3 d6 3. d4 cxd4) 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 *");
    const repertoire::Drill drill(g, core::Color::White, core::Game::root(), 0, 5);
    EXPECT_EQ(drill.trainable(), 5U); // e4; Nf3 (after c5); Nf3, Bb5, Ba4 (after e5)
}

TEST(Drill, FocusPlaysOnlyTheLinesThroughTheMistakes) {
    const core::Game g = game("1. e4 e5 (1... c5 2. Nf3 d6 3. d4) 2. Nf3 Nc6 3. Bb5 *");
    // The position after 1. e4 c5: the Sicilian line only.
    const std::unordered_set<std::uint64_t> focus{after({"e4", "c5"}).hash()};
    repertoire::Drill drill(g, core::Color::White, core::Game::root(), 0, 9, &focus);
    EXPECT_EQ(drill.trainable(), 1U); // 2. Nf3 there
    EXPECT_EQ(drill.lines(), 1U);
    EXPECT_EQ(playThrough(drill, g), std::vector<std::string>{"e4 c5 Nf3 d6 d4 "});
    EXPECT_TRUE(drill.finished());
}

TEST(Drill, AnAnswerSaysWhetherItWasTheFirstTry) {
    const core::Game g = game("1. e4 e5 2. Nf3 *");
    repertoire::Drill drill(g, core::Color::White, core::Game::root(), 0, 1);
    const auto wrong = drill.play(moveIn(g, core::Game::root(), "d4"));
    EXPECT_TRUE(wrong.first);
    EXPECT_EQ(wrong.position, core::Position().hash());
    const auto right = drill.play(wrong.expected);
    EXPECT_TRUE(right.correct);
    EXPECT_FALSE(right.first); // after a mistake here
}

namespace {

/// A made-up explorer: what is played after each line (by SAN), and which lines were asked.
struct FakeExplorer {
    std::map<std::string, std::vector<std::pair<std::string, std::uint64_t>>> played;
    std::vector<std::string> asked;

    repertoire::Explore function() {
        return [this](std::span<const core::Move> moves) {
            core::Position position;
            std::string key;
            for (const core::Move move : moves) {
                key += (key.empty() ? "" : " ") + position.san(move);
                position.play(move);
            }
            asked.push_back(key);
            std::vector<repertoire::MoveCount> result;
            for (const auto& [san, games] : played[key]) {
                result.push_back({position.parseSan(san).value(), games});
            }
            return result;
        };
    }
};

} // namespace

TEST(RepertoireGaps, FindsCommonRepliesTheRepertoireDoesNotAnswer) {
    // White: 1. e4, against 1... e5 2. Nf3, against 1... c5 2. Nf3; nothing against 1... e6.
    repertoire::Tree tree(core::Color::White);
    tree.add(game("1. e4 e5 (1... c5 2. Nf3) 2. Nf3 Nc6 3. Bb5 *"), 0, 0);
    FakeExplorer explorer;
    explorer.played[""] = {{"e4", 500}, {"d4", 400}};
    explorer.played["e4"] = {{"e5", 400}, {"c5", 350}, {"e6", 200}, {"b6", 10}};
    explorer.played["e4 e5 Nf3"] = {{"Nc6", 300}, {"d6", 60}};
    explorer.played["e4 c5 Nf3"] = {{"d6", 100}, {"Nc6", 80}};
    const auto report = repertoire::findGaps(tree, explorer.function());
    // It's never asked about our own moves (1. e4 is ours), only where the opponent moves.
    EXPECT_EQ(std::count(explorer.asked.begin(), explorer.asked.end(), ""), 0);
    // 1... e6 and 2... d6; after 1... c5 2. Nf3 the file just ends: lines end, that's no gap.
    ASSERT_EQ(report.gaps.size(), 2U);
    // 1... e6: a quarter of the games, at once: the most likely gap.
    EXPECT_EQ(report.gaps[0].san, "e6");
    EXPECT_TRUE(report.gaps[0].line == line({"e4"}));
    EXPECT_NEAR(report.gaps[0].share, 200.0 / 960.0, 1e-9);
    EXPECT_NEAR(report.gaps[0].likelihood, 200.0 / 960.0, 1e-9);
    // 1... b6 is too rare to matter (1%).
    for (const auto& gap : report.gaps) {
        EXPECT_NE(gap.san, "b6");
    }
    // Deeper: 2... d6 after 1... e5 2. Nf3 is reached in 400/960 of the games, then 1/6.
    const auto d6 = std::find_if(report.gaps.begin(), report.gaps.end(), [](const auto& gap) {
        return gap.san == "d6" && gap.line.size() == 3;
    });
    ASSERT_NE(d6, report.gaps.end());
    EXPECT_NEAR(d6->likelihood, (400.0 / 960.0) * (60.0 / 360.0), 1e-9);
    // Most likely first.
    for (std::size_t k = 1; k < report.gaps.size(); ++k) {
        EXPECT_GE(report.gaps[k - 1].likelihood, report.gaps[k].likelihood);
    }
    EXPECT_FALSE(report.stopped);
}

TEST(RepertoireGaps, TooFewGamesOrTooManyPositionsStopIt) {
    repertoire::Tree tree(core::Color::Black);
    tree.add(game("1. e4 c5 2. Nf3 d6 *"), 0, 0);
    FakeExplorer explorer;
    explorer.played[""] = {{"e4", 5}, {"d4", 4}}; // 9 games: too few to judge
    EXPECT_TRUE(repertoire::findGaps(tree, explorer.function()).gaps.empty());
    explorer.played[""] = {{"e4", 500}, {"d4", 400}};
    repertoire::GapOptions options;
    options.maxPositions = 1;
    const auto report = repertoire::findGaps(tree, explorer.function(), options);
    EXPECT_EQ(report.asked, 1U);
    ASSERT_EQ(report.gaps.size(), 1U);
    EXPECT_EQ(report.gaps[0].san, "d4"); // Black has nothing against 1. d4
    EXPECT_TRUE(report.stopped);         // 2. Nf3 wasn't looked at
}
