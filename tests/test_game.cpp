#include "chessgui/core/game.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string_view>

namespace {

using namespace chessgui::core;

/// Adds SAN moves after `parent` and returns the last node; fails the test on a bad move.
NodeId line(Game& game, NodeId parent, std::initializer_list<std::string_view> sans) {
    const std::vector<std::string_view> moves(sans);
    auto result = game.addLine(parent, moves);
    EXPECT_TRUE(result.ok()) << (result.ok() ? "" : result.error().message);
    return result.ok() ? *result : kNoNode;
}

std::vector<std::string> sansOf(const Game& game, const std::vector<NodeId>& ids) {
    std::vector<std::string> result;
    for (const NodeId id : ids) {
        result.push_back(game.node(id).san);
    }
    return result;
}

TEST(Game, NewGame) {
    const Game game;
    EXPECT_EQ(game.nodeCount(), 1U);
    EXPECT_EQ(game.mainChild(Game::root()), kNoNode);
    EXPECT_EQ(game.header("Result"), "*");
    EXPECT_EQ(game.header("White"), "?");
    EXPECT_EQ(game.header("FEN"), "");
    EXPECT_EQ(game.positionAt(Game::root()).fen(), kStartFen);
}

TEST(Game, MainLine) {
    Game game;
    const NodeId last = line(game, Game::root(), {"e4", "e5", "Nf3", "Nc6", "Bb5"});
    EXPECT_EQ(game.nodeCount(), 6U);
    const auto main = game.mainLine();
    ASSERT_EQ(main.size(), 6U);
    EXPECT_EQ(main.back(), last);
    EXPECT_EQ(sansOf(game, {main.begin() + 1, main.end()}),
              (std::vector<std::string>{"e4", "e5", "Nf3", "Nc6", "Bb5"}));
    EXPECT_EQ(game.positionAt(last).fen(),
              "r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3");
    EXPECT_EQ(game.ply(last), 5);
    EXPECT_EQ(game.moveNumber(last), 3);
    EXPECT_EQ(game.mover(last), Color::White);
    EXPECT_EQ(game.moveNumber(game.parent(last)), 2);
    EXPECT_EQ(game.mover(game.parent(last)), Color::Black);
    EXPECT_TRUE(game.isMainLine(last));
}

TEST(Game, AddingAnExistingMoveReturnsIt) {
    Game game;
    const NodeId e4 = line(game, Game::root(), {"e4"});
    EXPECT_EQ(*game.addSan(Game::root(), "e4"), e4);
    EXPECT_EQ(game.nodeCount(), 2U);
}

TEST(Game, Variations) {
    Game game;
    const NodeId e4 = line(game, Game::root(), {"e4"});
    const NodeId e5 = line(game, e4, {"e5"});
    const NodeId c5 = line(game, e4, {"c5"});
    const NodeId e6 = line(game, e4, {"e6"});
    EXPECT_EQ(game.children(e4), (std::vector<NodeId>{e5, c5, e6}));
    EXPECT_EQ(game.mainChild(e4), e5);
    EXPECT_EQ(game.nextSibling(e5), c5);
    EXPECT_FALSE(game.isMainLine(c5));
    EXPECT_TRUE(game.isMainLine(e5));

    game.promote(e6);
    EXPECT_EQ(game.children(e4), (std::vector<NodeId>{e5, e6, c5}));
    game.promote(e6);
    EXPECT_EQ(game.children(e4), (std::vector<NodeId>{e6, e5, c5}));
    game.promote(e6); // already first: no change
    EXPECT_EQ(game.children(e4), (std::vector<NodeId>{e6, e5, c5}));
    EXPECT_TRUE(game.isMainLine(e6));
}

TEST(Game, MakeMainLineWorksAllTheWayUp) {
    Game game;
    line(game, Game::root(), {"e4", "e5", "Nf3"});
    const NodeId d4 = line(game, Game::root(), {"d4"});
    const NodeId d5 = line(game, d4, {"d5"});
    const NodeId nf6 = line(game, d4, {"Nf6"});
    const NodeId c4 = line(game, nf6, {"c4"});
    EXPECT_FALSE(game.isMainLine(c4));

    game.makeMainLine(c4);
    EXPECT_TRUE(game.isMainLine(c4));
    EXPECT_EQ(sansOf(game, game.mainLine()), (std::vector<std::string>{"", "d4", "Nf6", "c4"}));
    EXPECT_EQ(game.children(d4), (std::vector<NodeId>{nf6, d5}));
}

TEST(Game, RemoveDeletesTheSubtreeAndReusesIds) {
    Game game;
    const NodeId e4 = line(game, Game::root(), {"e4"});
    line(game, e4, {"e5", "Nf3", "Nc6"});
    const NodeId c5 = line(game, e4, {"c5"});
    line(game, c5, {"Nf3", "d6"});
    EXPECT_EQ(game.nodeCount(), 8U); // root, e4, 3 main-line moves, 3 in the variation

    game.remove(c5);
    EXPECT_EQ(game.nodeCount(), 5U);
    EXPECT_EQ(game.children(e4).size(), 1U);

    const NodeId reused = line(game, e4, {"d5"});
    EXPECT_LT(reused, 8U); // took a freed slot instead of growing the array
    EXPECT_EQ(game.node(reused).san, "d5");
    EXPECT_EQ(game.node(reused).comment, "");
}

TEST(Game, RemoveFirstChildPromotesTheNext) {
    Game game;
    const NodeId e4 = line(game, Game::root(), {"e4"});
    const NodeId d4 = line(game, Game::root(), {"d4"});
    game.remove(e4);
    EXPECT_EQ(game.mainChild(Game::root()), d4);
}

TEST(Game, RemoveVariationRemovesFromTheBranchPoint) {
    Game game;
    const NodeId e4 = line(game, Game::root(), {"e4"});
    line(game, e4, {"e5", "Nf3"});
    const NodeId deep = line(game, e4, {"c5", "Nf3", "d6", "d4"});
    EXPECT_EQ(game.nodeCount(), 8U);

    game.removeVariation(deep); // deletes 1...c5 and everything after it
    EXPECT_EQ(game.nodeCount(), 4U);
    EXPECT_EQ(game.children(e4).size(), 1U);

    // On the main line it deletes from the node onwards.
    const NodeId nf3 = game.mainLine().back();
    game.removeVariation(nf3);
    EXPECT_EQ(game.mainLine().size(), 3U);
}

TEST(Game, IllegalSanIsReportedWithMoveNumber) {
    Game game;
    const NodeId e4 = line(game, Game::root(), {"e4"});
    const auto bad = game.addSan(e4, "Ke2");
    ASSERT_FALSE(bad.ok());

    const std::array<std::string_view, 3> moves{"e5", "Nf3", "Bxe5"};
    const auto result = game.addLine(e4, moves);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().message, "2... \"Bxe5\" is not a legal move here.");
    // The moves before the bad one were added.
    EXPECT_EQ(game.mainLine().size(), 4U);
}

TEST(Game, FromFenWithBlackToMove) {
    auto start = Position::fromFen("4k3/8/8/8/8/8/4P3/4K3 b - - 0 30");
    ASSERT_TRUE(start.ok());
    Game game(*start);
    EXPECT_EQ(game.header("SetUp"), "1");
    EXPECT_EQ(game.header("FEN"), "4k3/8/8/8/8/8/4P3/4K3 b - - 0 30");

    const NodeId kd7 = line(game, Game::root(), {"Kd7"});
    const NodeId e4 = line(game, kd7, {"e4"});
    EXPECT_EQ(game.moveNumber(kd7), 30);
    EXPECT_EQ(game.mover(kd7), Color::Black);
    EXPECT_EQ(game.moveNumber(e4), 31);
    EXPECT_EQ(game.mover(e4), Color::White);
}

TEST(Game, Annotations) {
    Game game;
    const NodeId e4 = line(game, Game::root(), {"e4"});
    game.setComment(e4, "Best by test");
    game.setCommentBefore(e4, "The main line:");
    game.setNags(e4, {nag::kGood});
    game.setArrows(e4, {{*Square::parse("e2"), *Square::parse("e4"), MarkColor::Green}});
    game.setSquareMarks(e4, {{*Square::parse("d5"), MarkColor::Red}});

    const Node& node = game.node(e4);
    EXPECT_EQ(node.comment, "Best by test");
    EXPECT_EQ(node.commentBefore, "The main line:");
    EXPECT_EQ(node.nags, (std::vector<std::uint8_t>{nag::kGood}));
    EXPECT_EQ(node.arrows.size(), 1U);
    EXPECT_EQ(node.squareMarks.size(), 1U);
}

TEST(Game, Headers) {
    Game game;
    game.setHeader("White", "Carlsen, Magnus");
    game.setHeader("ECO", "C65");
    EXPECT_EQ(game.header("White"), "Carlsen, Magnus");
    EXPECT_EQ(game.header("ECO"), "C65");
    EXPECT_EQ(game.headers().front().first, "Event"); // order kept
    game.removeHeader("ECO");
    EXPECT_EQ(game.header("ECO"), "");
}

TEST(Game, CopyIsIndependent) {
    Game game;
    line(game, Game::root(), {"e4", "e5"});
    Game copy = game;
    line(copy, copy.mainLine().back(), {"Nf3"});
    EXPECT_EQ(game.mainLine().size(), 3U);
    EXPECT_EQ(copy.mainLine().size(), 4U);
}

} // namespace
