// The Puzzles page: puzzles from a Library file (solved in order, a wrong move taken back, hints,
// the solution) and from the Lichess puzzle database (imported from a local file here: tests
// never use the network), with the stats they add up to.
#include "MainWindow.hpp"
#include "board/BoardView.hpp"
#include "kiebitz/puzzles/puzzle.hpp"
#include "library/Library.hpp"
#include "puzzles/LichessPuzzles.hpp"
#include "puzzles/PuzzlesView.hpp"
#include "style/AppStyle.hpp"
#include "widgets/Stepper.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>
#include <gtest/gtest.h>

namespace {

using kiebitz::app::AppStyle;
using kiebitz::app::BoardView;
using kiebitz::app::LichessPuzzles;
using kiebitz::app::MainWindow;
using kiebitz::app::PuzzlesView;
using namespace kiebitz;

// From a puzzle file like the Wolga tactics: Black to move; the main line is the solution, the
// variation another answer of White's. The third has no solution.
constexpr const char* kPuzzleFile = R"([White "Aufgabe 1"]
[Black "Standardmotiv"]
[SetUp "1"]
[FEN "5rk1/3nppbp/3p2p1/qNpP4/Pr2P3/6P1/1P3PKP/R1B1Q2R b - - 0 1"]
[FileType "Puzzle"]

{Schwarz am Zug!} 1... Rxb5 2. axb5 (2. Qxa5 Rxa5) 2... Qxa1 *

[White "Aufgabe 2"]
[Black "Standardmotiv"]
[SetUp "1"]
[FEN "rr4k1/1qnnppbp/3p2p1/2pP4/P3P3/RPN3P1/3BQPKP/1R2N3 b - - 0 1"]
[FileType "Puzzle"]

1... Bxc3 2. Bxc3 Nb5 3. axb5 Rxa3 *

[White "Aufgabe 3"]
[SetUp "1"]
[FEN "5rk1/3nppbp/3p2p1/qNpP4/Pr2P3/6P1/1P3PKP/R1B1Q2R b - - 0 1"]
[FileType "Puzzle"]

{Schwarz am Zug!} *
)";

constexpr const char* kLichessCsv =
    "PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags\n"
    "0000D,5rk1/1p3ppp/pq3b2/8/8/1P1Q1N2/P4PPP/3R2K1 w - - 2 27,d3d6 f8d8 d6d8 f6d8,1433,75,96,"
    "37700,advantage endgame short,https://lichess.org/game/F8M8OS71#53,\n"
    "0000H,r4rk1/pp3ppp/8/2bqp2b/3N4/P2Bn2P/1PPN1P2/R1BQ1RK1 w - - 1 15,d1h5 d5g2,1032,78,100,92,"
    "mate mateIn1 middlegame oneMove,https://lichess.org/game/EwHRkfas#29,\n";

void writeFile(const QString& path, const char* text) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(text);
}

/// A move made with the mouse: a click on the piece, then on its square.
void clickMove(BoardView* board, const core::Move& move) {
    QTest::mouseClick(board, Qt::LeftButton, {}, board->squareRect(move.from).center());
    QTest::mouseClick(board, Qt::LeftButton, {}, board->squareRect(move.to).center());
}

core::Move san(const BoardView* board, std::string_view text) {
    return board->position().parseSan(text).value();
}

class PuzzlesTest : public ::testing::Test {
protected:
    void SetUp() override {
        window.resize(1200, 800);
        window.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
        view = window.findChild<PuzzlesView*>();
        ASSERT_NE(view, nullptr);
    }
    void snapshot(const char* name) {
        if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
            window.resize(1300, 820);
            QTest::qWait(150);
            window.grab().save(QDir(dir).filePath(QString::fromLatin1(name)));
        }
    }

    QTemporaryDir folder;
    AppStyle style{*qApp};
    MainWindow window{style};
    PuzzlesView* view = nullptr;
};

TEST_F(PuzzlesTest, APuzzleFileIsSolvedInOrderWithRetriesHintsAndTheSolution) {
    const QString file = folder.filePath(QStringLiteral("tactics.pgn"));
    writeFile(file, kPuzzleFile);
    QSignalSpy scanned(&window.library(), &kiebitz::app::Library::scanned);
    window.library().setFolder(folder.path());
    ASSERT_TRUE(scanned.wait(5000));

    // The Library's "Solve" on the puzzle file opens it on the Puzzles page.
    window.showSection(MainWindow::Section::Library);
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("libraryFiles"));
    tree->setCurrentItem(
        tree->findItems(QStringLiteral("tactics"), Qt::MatchExactly | Qt::MatchRecursive).value(0));
    auto* solve = window.findChild<QPushButton*>(QStringLiteral("librarySolvePuzzles"));
    ASSERT_NE(solve, nullptr);
    ASSERT_TRUE(QTest::qWaitFor([&] { return solve->isVisible(); }, 5000));
    QSignalSpy shown(view, &PuzzlesView::puzzleShown);
    QTest::mouseClick(solve, Qt::LeftButton);
    EXPECT_EQ(window.section(), MainWindow::Section::Puzzles);
    ASSERT_TRUE(shown.wait(5000));
    EXPECT_EQ(view->source(), file);
    EXPECT_EQ(view->prompt(), QStringLiteral("Black to move"));
    const auto* details = window.findChild<QLabel*>(QStringLiteral("puzzleDetails"));
    EXPECT_EQ(details->text(), QStringLiteral("Aufgabe 1  ·  Standardmotiv\nSchwarz am Zug!"));
    BoardView* board = view->board();
    EXPECT_TRUE(board->isFlipped());
    // The file's three puzzles; the last one has no solution.
    EXPECT_EQ(view->listRows(), (QStringList{QStringLiteral("Aufgabe 1  ·  Standardmotiv"),
                                             QStringLiteral("Aufgabe 2  ·  Standardmotiv"),
                                             QStringLiteral("Aufgabe 3  no solution")}));

    // A wrong move is taken back.
    const std::string start = board->position().fen();
    clickMove(board, san(board, "Qa7"));
    EXPECT_EQ(view->prompt(), QStringLiteral("Not that move. Try again."));
    EXPECT_EQ(board->position().fen(), start);
    // The right one, and White answers.
    clickMove(board, san(board, "Rxb5"));
    EXPECT_EQ(view->prompt(), QStringLiteral("Right! Keep going."));
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return board->position().sideToMove() == core::Color::Black; }, 5000));
    clickMove(board, san(board, "Qxa1"));
    EXPECT_EQ(view->prompt(), QStringLiteral("Solved, after a miss."));
    ASSERT_TRUE(view->solving()->solved());
    auto* solvedTile = window.findChild<QLabel*>(QStringLiteral("puzzleSolvedTile"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return solvedTile->text() == u"1"; }, 5000));
    EXPECT_EQ(window.findChild<QLabel*>(QStringLiteral("puzzleStreakTile"))->text(), u"0");

    // Next: the second (shown at once: a file's puzzle needs no reading). A hint shows the
    // piece, a second one the move.
    const auto before = shown.count();
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("puzzleNext")), Qt::LeftButton);
    ASSERT_TRUE(QTest::qWaitFor([&] { return shown.count() > before; }, 5000));
    EXPECT_EQ(details->text(), QStringLiteral("Aufgabe 2  ·  Standardmotiv"));
    auto* hint = window.findChild<QPushButton*>(QStringLiteral("puzzleHint"));
    QTest::mouseClick(hint, Qt::LeftButton);
    const core::Move bxc3 = san(board, "Bxc3");
    ASSERT_EQ(board->squareMarks().size(), 1U);
    EXPECT_EQ(board->squareMarks()[0].square, bxc3.from);
    QTest::mouseClick(hint, Qt::LeftButton);
    ASSERT_EQ(board->arrows().size(), 1U);
    EXPECT_EQ(board->arrows()[0].to, bxc3.to);
    snapshot("puzzles-file.png");
    // The solution plays out.
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("puzzleSolution")),
                      Qt::LeftButton);
    ASSERT_TRUE(QTest::qWaitFor([&] { return view->solving()->solved(); }, 10000));
    EXPECT_EQ(view->prompt(), QStringLiteral("Solved, with help."));
    EXPECT_EQ(board->position().fen(),
              "1r4k1/1q1npp1p/3p2p1/1PpP4/4P3/rPB3P1/4QPKP/1R2N3 w - - 0 4");
    // ← steps back through the solution.
    QTest::keyClick(board, Qt::Key_Left);
    EXPECT_EQ(board->position().sideToMove(), core::Color::Black);
}

TEST_F(PuzzlesTest, LichessPuzzlesAreImportedPickedByRatingAndSolved) {
    window.showSection(MainWindow::Section::Puzzles);
    auto* lichess = window.findChild<LichessPuzzles*>();
    ASSERT_NE(lichess, nullptr);
    // Not there yet: the page offers the download.
    auto* download = window.findChild<QPushButton*>(QStringLiteral("puzzleDownload"));
    ASSERT_NE(download, nullptr);
    EXPECT_TRUE(download->isVisible());
    snapshot("puzzles-download.png");

    // Imported from a file on disk instead of the download.
    const QString csv = folder.filePath(QStringLiteral("puzzles.csv"));
    writeFile(csv, kLichessCsv);
    QSignalSpy shown(view, &PuzzlesView::puzzleShown);
    lichess->importFile(csv, false);
    ASSERT_TRUE(shown.wait(10000));
    EXPECT_EQ(lichess->state(), LichessPuzzles::State::Ready);
    // From 1200 to 1800: 0000D (1433). White played Qd6; Black solves.
    EXPECT_EQ(view->prompt(), QStringLiteral("Black to move"));
    BoardView* board = view->board();
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return board->position().sideToMove() == core::Color::Black; }, 5000));
    EXPECT_EQ(window.findChild<QLabel*>(QStringLiteral("puzzleDetails"))->text(),
              QStringLiteral("Rating 1433  ·  Advantage, Endgame, Short"));
    clickMove(board, san(board, "Rd8"));
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return board->position().sideToMove() == core::Color::Black; }, 5000));
    clickMove(board, san(board, "Bxd8"));
    EXPECT_EQ(view->prompt(), QStringLiteral("Solved!"));
    auto* solvedTile = window.findChild<QLabel*>(QStringLiteral("puzzleSolvedTile"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return solvedTile->text() == u"1"; }, 5000));
    EXPECT_TRUE(view->listRows().value(0).startsWith(u"Solved  1433  0:0"));
    snapshot("puzzles-lichess.png");
    // The next one comes by itself (the same here: the only one in the range).
    ASSERT_TRUE(shown.wait(5000));
    EXPECT_EQ(view->prompt(), QStringLiteral("Black to move"));
}

} // namespace

namespace {

TEST_F(PuzzlesTest, TheRangeFollowsYourRatingUntilYouSetOneAndTheClockKeepsItsPlace) {
    auto* low = window.findChild<kiebitz::app::Stepper*>(QStringLiteral("puzzleLow"));
    const auto* high = window.findChild<kiebitz::app::Stepper*>(QStringLiteral("puzzleHigh"));
    ASSERT_NE(low, nullptr);
    // Your best rating, 2034: puzzles from 1900 to 2200.
    int rating = 2034;
    view->setRatingSource([&] { return rating; });
    EXPECT_EQ(low->value(), 1900);
    EXPECT_EQ(high->value(), 2200);
    rating = 2160; // after a sync
    view->followRating();
    EXPECT_EQ(low->value(), 2000);
    // A range of your own stays.
    emit low->valueChanged(1500);
    low->setValue(1500);
    rating = 2400;
    view->followRating();
    EXPECT_EQ(low->value(), 1500);

    // The clock is as wide for 0:00 as for 12:34: nothing beside it moves.
    auto* clock = window.findChild<QLabel*>(QStringLiteral("puzzleClock"));
    const int width = clock->width();
    clock->setText(QStringLiteral("12:34"));
    QApplication::processEvents();
    EXPECT_EQ(clock->width(), width);
    EXPECT_GE(clock->width(), clock->fontMetrics().horizontalAdvance(QStringLiteral("12:34")));

    // Hiding the rating makes the details shorter, but they keep their height.
    auto* details = window.findChild<QLabel*>(QStringLiteral("puzzleDetails"));
    const int detailsHeight = details->height();
    details->setText(QStringLiteral("Rating hidden"));
    QApplication::processEvents();
    EXPECT_EQ(details->height(), detailsHeight);
    EXPECT_GE(detailsHeight, 2 * details->fontMetrics().lineSpacing());

    // The clock can be stopped, and started again.
    auto* pause = window.findChild<QPushButton*>(QStringLiteral("puzzlePause"));
    ASSERT_NE(pause, nullptr);
    EXPECT_EQ(pause->toolTip(), QStringLiteral("Stop the clock"));
    window.showSection(MainWindow::Section::Puzzles);
    QTest::mouseClick(pause, Qt::LeftButton);
    EXPECT_EQ(pause->toolTip(), QStringLiteral("Start the clock"));
    EXPECT_FALSE(clock->isEnabled());
    QTest::mouseClick(pause, Qt::LeftButton);
    EXPECT_EQ(pause->toolTip(), QStringLiteral("Stop the clock"));
}

} // namespace
