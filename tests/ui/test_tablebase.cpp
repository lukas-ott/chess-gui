// The endgame tablebase in the Explorer: from 7 pieces it shows who wins and how each move
// scores, asked from a fake server (tests never use the network).

#include "databases/DatabaseManager.hpp"
#include "explorer/ExplorerPanel.hpp"
#include "explorer/Tablebase.hpp"
#include "fake_server.hpp"
#include "library/Library.hpp"
#include "net/Lichess.hpp"
#include "profiles/LichessLogin.hpp"
#include "profiles/ProfileStore.hpp"
#include "repertoire/RepertoireStore.hpp"
#include "style/AppStyle.hpp"

#include <QApplication>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <gtest/gtest.h>

namespace {

using kiebitz::app::Tablebase;
using namespace kiebitz;
namespace lichess = kiebitz::app::lichess;

/// King and queen against king, White to move: Lichess's answer, cut down to three moves.
/// Each move's category, dtz and dtm there are for Black (to move after it).
const char* const kQueenEnding =
    R"({"checkmate":false,"stalemate":false,"dtz":9,"dtm":9,"category":"win","moves":[)"
    R"({"uci":"d2d6","san":"Qd6","checkmate":false,"stalemate":false,"dtz":-8,"dtm":-8,)"
    R"("category":"loss"},)"
    R"({"uci":"d2d7","san":"Qd7","checkmate":false,"stalemate":false,"dtz":-10,"dtm":-10,)"
    R"("category":"loss"},)"
    R"({"uci":"d2d4","san":"Qd4+","checkmate":false,"stalemate":false,"dtz":0,"dtm":0,)"
    R"("category":"draw"}]})";
const char* const kQueenFen = "8/8/8/8/4k3/8/3QK3/8 w - - 0 1";

TEST(Tablebase, CoversSevenPiecesWithoutCastling) {
    EXPECT_TRUE(Tablebase::covers(*core::Position::fromFen(kQueenFen)));
    EXPECT_FALSE(Tablebase::covers(core::Position())); // 32 pieces
    // Few pieces, but castling rights: not in the tablebase.
    EXPECT_FALSE(Tablebase::covers(*core::Position::fromFen("4k3/8/8/8/8/8/8/4K2R w K - 0 1")));
    EXPECT_TRUE(Tablebase::covers(*core::Position::fromFen("4k3/8/8/8/8/8/8/4K2R w - - 0 1")));
}

TEST(Tablebase, ReadsEachMoveForTheSidePlayingIt) {
    const auto position = *core::Position::fromFen(kQueenFen);
    const Tablebase::Answer answer = Tablebase::parse(kQueenEnding, position);
    EXPECT_TRUE(answer.error.isEmpty());
    EXPECT_EQ(answer.outcome, Tablebase::Outcome::Win); // White, to move, wins
    EXPECT_EQ(answer.dtm, 9);
    ASSERT_EQ(answer.moves.size(), 3U);
    EXPECT_EQ(answer.moves[0].san, "Qd6");
    EXPECT_EQ(answer.moves[0].move.uci(), "d2d6");
    EXPECT_EQ(answer.moves[0].outcome, Tablebase::Outcome::Win); // Black's loss: White's win
    EXPECT_EQ(answer.moves[0].dtz, 8);                           // positive: plies
    EXPECT_EQ(answer.moves[0].dtm, 8);
    EXPECT_EQ(answer.moves[2].outcome, Tablebase::Outcome::Draw); // Qd4+?? Kxd4
    EXPECT_FALSE(Tablebase::parse("not json", position).error.isEmpty());
}

class TablebaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        lichess::endPause();
        server.pages["/standard"] = kQueenEnding;
        explorer.tablebase().setServer(server.url());
        explorer.resize(560, 360);
        explorer.show();
    }
    QTemporaryDir folder;
    test::FakeServer server;
    app::AppStyle style{*qApp};
    app::DatabaseManager databases{folder.filePath(QStringLiteral("dbs"))};
    app::Library library;
    app::RepertoireStore repertoires{library};
    app::ProfileStore profiles{folder.filePath(QStringLiteral("profiles"))};
    app::LichessLogin login{app::LichessLogin::Storage{}};
    app::ExplorerPanel explorer{databases, profiles, repertoires, login, style};
};

TEST_F(TablebaseTest, FromSevenPiecesTheExplorerShowsTheTablebase) {
    // A game from a set-up position: the tablebase answers (the database explorer couldn't).
    const core::Game game(*core::Position::fromFen(kQueenFen));
    explorer.setPosition(&game, core::Game::root(), core::Color::White);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !explorer.tablebaseTexts().isEmpty(); }, 5000));
    EXPECT_EQ(explorer.tablebaseTexts(),
              (QStringList{QStringLiteral("Qd6 Win 8 8"), QStringLiteral("Qd7 Win 10 10"),
                           QStringLiteral("Qd4+ Draw – –")}));
    const auto* summary = explorer.findChild<QLabel*>(QStringLiteral("explorerSummary"));
    EXPECT_EQ(summary->text(), QStringLiteral("Tablebase  ·  White wins  ·  mate in 5"));
    EXPECT_TRUE(server.asked.front().startsWith(u"/standard?fen=8/8/8/8/4k3"));
    EXPECT_TRUE(server.requests.front().contains("User-Agent: Kiebitz/"));

    // A click plays the move.
    auto* list = explorer.findChild<QTreeWidget*>(QStringLiteral("explorerTablebase"));
    ASSERT_NE(list, nullptr);
    QSignalSpy played(&explorer, &app::ExplorerPanel::movePlayed);
    emit list->itemClicked(list->topLevelItem(0), 0);
    ASSERT_EQ(played.size(), 1);
    EXPECT_EQ(played.front().at(0).value<core::Move>().uci(), "d2d6");

    // The same position again: remembered, not asked again.
    const qsizetype asked = server.asked.size();
    explorer.setPosition(&game, core::Game::root(), core::Color::White);
    EXPECT_FALSE(explorer.tablebaseTexts().isEmpty());
    EXPECT_EQ(server.asked.size(), asked);

    // A full board: the usual explorer again.
    const core::Game start;
    explorer.setPosition(&start, core::Game::root(), core::Color::White);
    EXPECT_TRUE(explorer.tablebaseTexts().isEmpty());
}

TEST_F(TablebaseTest, WhenTheTablebaseDoesNotAnswerItSaysSo) {
    server.statuses["/standard"] = 500;
    const core::Game game(*core::Position::fromFen(kQueenFen));
    explorer.setPosition(&game, core::Game::root(), core::Color::White);
    const auto* summary = explorer.findChild<QLabel*>(QStringLiteral("explorerSummary"));
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return summary->text() == u"The tablebase isn't answering right now."; }, 5000));
    EXPECT_TRUE(explorer.tablebaseTexts().isEmpty());
}

} // namespace
