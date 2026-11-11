#include "MainWindow.hpp"
#include "board/BoardView.hpp"
#include "notation/NotationView.hpp"
#include "style/AppStyle.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QTabBar>
#include <QTest>
#include <QToolButton>
#include <gtest/gtest.h>

#include <algorithm>

namespace {

using kiebitz::app::BoardView;
using kiebitz::app::MainWindow;
using kiebitz::app::NotationView;
using namespace kiebitz::core;

/// Finds a menu action by its text (without the & mnemonic).
QAction* action(MainWindow& window, const QString& text) {
    for (auto* candidate : window.findChildren<QAction*>()) {
        if (candidate->text().remove(QLatin1Char('&')) == text) {
            return candidate;
        }
    }
    return nullptr;
}

class MainWindowTest : public ::testing::Test {
protected:
    void SetUp() override {
        window.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
        board = window.findChild<BoardView*>();
        ASSERT_NE(board, nullptr);
        // The app starts in the Library; the game tests work on the board.
        window.showSection(MainWindow::Section::Board);
    }
    void play(std::string_view san) {
        const Move move = board->position().parseSan(san).value();
        emit board->moveMade(move);
    }
    [[nodiscard]] std::string fen() const { return board->position().fen(); }

    kiebitz::app::AppStyle style{*qApp};
    MainWindow window{style};
    BoardView* board = nullptr;
};

TEST_F(MainWindowTest, MovesAreAddedAndCanBeNavigated) {
    play("e4");
    play("e5");
    play("Nf3");
    const std::string afterNf3 = fen();

    action(window, QStringLiteral("Back"))->trigger();
    action(window, QStringLiteral("Back"))->trigger();
    EXPECT_EQ(fen(), "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");

    action(window, QStringLiteral("Go to End"))->trigger();
    EXPECT_EQ(fen(), afterNf3);
    EXPECT_FALSE(action(window, QStringLiteral("Forward"))->isEnabled());

    action(window, QStringLiteral("Go to Start"))->trigger();
    EXPECT_EQ(fen(), std::string(kStartFen));
    EXPECT_FALSE(action(window, QStringLiteral("Back"))->isEnabled());

    action(window, QStringLiteral("Forward"))->trigger();
    EXPECT_EQ(fen(), "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
}

TEST_F(MainWindowTest, DrawingsStayWithTheirMove) {
    play("e4");
    emit board->drawingsChanged(
        {{Square::parse("d2").value(), Square::parse("d4").value(), MarkColor::Green}}, {});
    play("e5");
    EXPECT_TRUE(board->arrows().empty()); // a new move has no drawings yet
    action(window, QStringLiteral("Back"))->trigger();
    EXPECT_EQ(board->arrows().size(), 1U); // back on 1. e4: its arrow is shown again
}

TEST_F(MainWindowTest, FirstLaunchFillsTheScreen) {
    // No saved window geometry (the tests' settings are empty): zoomed, but not full screen.
    EXPECT_TRUE(window.windowState().testFlag(Qt::WindowMaximized));
    EXPECT_FALSE(window.windowState().testFlag(Qt::WindowFullScreen));
}

TEST_F(MainWindowTest, TabsAreNeverSqueezedByASmallWindow) {
    auto* tabs = window.findChild<QTabBar*>(QStringLiteral("documentTabs"));
    ASSERT_NE(tabs, nullptr);
    window.setWindowState(Qt::WindowNoState);
    window.resize(200, 200); // far too small: the window stops at its minimum size
    QApplication::processEvents();
    EXPECT_GE(window.width(), 960);
    EXPECT_GE(tabs->width(), tabs->sizeHint().width()); // every tab in full, no scroll arrows
}

TEST_F(MainWindowTest, NewTabStartsAFreshGame) {
    play("d4");
    const auto* tabs = window.findChild<QTabBar*>(QStringLiteral("documentTabs"));
    const int before = tabs->count();
    action(window, QStringLiteral("New Tab"))->trigger();
    EXPECT_EQ(tabs->count(), before + 1);
    EXPECT_EQ(fen(), std::string(kStartFen));
    // An untouched new game is reused instead of piling up empty tabs.
    action(window, QStringLiteral("New Tab"))->trigger();
    EXPECT_EQ(tabs->count(), before + 1);
}

TEST_F(MainWindowTest, FlipActionFlipsTheBoard) {
    const bool before = board->isFlipped();
    action(window, QStringLiteral("Flip Board"))->trigger();
    EXPECT_NE(board->isFlipped(), before);
    action(window, QStringLiteral("Flip Board"))->trigger();
    EXPECT_EQ(board->isFlipped(), before);
}

TEST_F(MainWindowTest, MoveListShowsMovesVariationsAndNumbers) {
    play("e4");
    play("e5");
    action(window, QStringLiteral("Back"))->trigger();
    play("c5"); // a variation at Black's first move
    play("Nf3");
    auto* list = window.findChild<NotationView*>(QStringLiteral("moveList"));
    ASSERT_NE(list, nullptr);
    const QString text = list->plainText().simplified().replace(QChar(0xA0), QLatin1Char(' '));
    // First-level variations get their own indented line, so no brackets here.
    EXPECT_EQ(text, QStringLiteral("1. e4 e5 1… c5 2. Nf3"));
}

TEST_F(MainWindowTest, ClickingAMoveInTheListGoesThere) {
    play("d4");
    play("d5");
    auto* list = window.findChild<NotationView*>(QStringLiteral("moveList"));
    ASSERT_NE(list, nullptr);
    const QRect d4 = list->moveRect(1); // node 1 = 1. d4
    ASSERT_FALSE(d4.isEmpty());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, d4.center());
    EXPECT_EQ(fen(), "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1");
}

/// Saves the whole window in dark and light when KIEBITZ_SNAPSHOT_DIR is set.
TEST_F(MainWindowTest, Snapshots) {
    window.resize(1200, 760);
    for (const char* san : {"e4", "e5", "Nf3", "Nc6", "Bb5", "a6", "Ba4", "Nf6", "O-O", "Be7"}) {
        play(san);
    }
    action(window, QStringLiteral("Back"))->trigger();
    action(window, QStringLiteral("Back"))->trigger();
    play("d3");
    emit board->drawingsChanged(
        {{Square::parse("c2").value(), Square::parse("c3").value(), MarkColor::Green},
         {Square::parse("b1").value(), Square::parse("d2").value(), MarkColor::Blue}},
        {});
    board->setDrawings(
        {{Square::parse("c2").value(), Square::parse("c3").value(), MarkColor::Green},
         {Square::parse("b1").value(), Square::parse("d2").value(), MarkColor::Blue}},
        {});
    QTest::keyClick(&window, Qt::Key_Left); // back to 4... Nf6: a branch point (5. O-O or 5. d3)
    QTest::keyClick(&window, Qt::Key_Down); // choose 5. d3
    const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR");
    for (const auto appearance :
         {kiebitz::app::AppStyle::Appearance::Dark, kiebitz::app::AppStyle::Appearance::Light}) {
        style.setAppearance(appearance);
        QApplication::processEvents();
        const QImage image = window.grab().toImage();
        EXPECT_FALSE(image.isNull());
        if (!dir.isEmpty()) {
            image.save(QDir(dir).filePath(
                QStringLiteral("window-%1.png").arg(kiebitz::app::AppStyle::toString(appearance))));
        }
    }
    style.setAppearance(kiebitz::app::AppStyle::Appearance::System);
}

/// The keys themselves, not just the menu items: on macOS plain-key menu shortcuts never fire,
/// so the window handles them.
TEST_F(MainWindowTest, ArrowKeysNavigate) {
    play("e4");
    play("e5");
    play("Nf3");
    const std::string afterNf3 = fen();

    QTest::keyClick(&window, Qt::Key_Left);
    QTest::keyClick(&window, Qt::Key_Left);
    EXPECT_EQ(fen(), "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    QTest::keyClick(&window, Qt::Key_Right);
    QTest::keyClick(&window, Qt::Key_Right);
    EXPECT_EQ(fen(), afterNf3);
    QTest::keyClick(&window, Qt::Key_Right); // at the end: nothing happens
    EXPECT_EQ(fen(), afterNf3);
    QTest::keyClick(&window, Qt::Key_Home);
    EXPECT_EQ(fen(), std::string(kStartFen));
    QTest::keyClick(&window, Qt::Key_End);
    EXPECT_EQ(fen(), afterNf3);

    // Also while the board has focus.
    board->setFocus();
    QTest::keyClick(board, Qt::Key_Left);
    EXPECT_NE(fen(), afterNf3);
}

TEST_F(MainWindowTest, FKeyFlipsTheBoard) {
    const bool before = board->isFlipped();
    QTest::keyClick(&window, Qt::Key_F);
    EXPECT_NE(board->isFlipped(), before);
    QTest::keyClick(&window, Qt::Key_F);
    EXPECT_EQ(board->isFlipped(), before);
}

TEST_F(MainWindowTest, UpAndDownJumpToStartAndEnd) {
    play("d4");
    play("d5");
    play("c4");
    const std::string end = fen();
    QTest::keyClick(&window, Qt::Key_Up);
    EXPECT_EQ(fen(), std::string(kStartFen));
    QTest::keyClick(&window, Qt::Key_Down);
    EXPECT_EQ(fen(), end);
}

TEST_F(MainWindowTest, UpAndDownChooseBetweenContinuations) {
    play("e4");
    play("e5");
    QTest::keyClick(&window, Qt::Key_Left);
    play("c5"); // second continuation after 1. e4
    QTest::keyClick(&window, Qt::Key_Left);
    play("e6");                             // third
    QTest::keyClick(&window, Qt::Key_Left); // back on 1. e4: three continuations

    // The board shows them as candidate arrows; the list offers them too.
    const auto buttons = window.findChildren<QPushButton*>();
    const auto forks = std::count_if(buttons.begin(), buttons.end(), [](QPushButton* button) {
        return button->property("role").toString() == QLatin1String("fork") && button->isVisible();
    });
    EXPECT_EQ(forks, 3);

    QTest::keyClick(&window, Qt::Key_Down); // choose 1... c5
    QTest::keyClick(&window, Qt::Key_Right);
    EXPECT_EQ(fen(), "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");

    QTest::keyClick(&window, Qt::Key_Left);
    QTest::keyClick(&window, Qt::Key_Up); // wraps around to the last: 1... e6
    QTest::keyClick(&window, Qt::Key_Right);
    EXPECT_EQ(fen(), "rnbqkbnr/pppp1ppp/4p3/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");

    QTest::keyClick(&window, Qt::Key_Left);
    QTest::keyClick(&window, Qt::Key_Right); // no choice made: the main line
    EXPECT_EQ(fen(), "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");
}

TEST_F(MainWindowTest, ContinuationHintShowsOnlyUntilFirstUse) {
    QSettings().remove(QStringLiteral("hints/continuationsLearned"));
    const auto hintVisible = [this] {
        const auto labels = window.findChildren<QLabel*>();
        return std::any_of(labels.begin(), labels.end(), [](QLabel* label) {
            return label->property("role").toString() == QLatin1String("hint") &&
                   label->isVisible();
        });
    };
    play("e4");
    QTest::keyClick(&window, Qt::Key_Left);
    play("d4");
    QTest::keyClick(&window, Qt::Key_Left); // a branch point for the first time
    EXPECT_TRUE(hintVisible());

    QTest::keyClick(&window, Qt::Key_Down); // the user chooses once...
    QTest::keyClick(&window, Qt::Key_Right);
    QTest::keyClick(&window, Qt::Key_Left);
    EXPECT_FALSE(hintVisible()); // ...and the hint is gone for good
}

} // namespace
