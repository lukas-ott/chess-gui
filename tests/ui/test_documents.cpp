// M3 in the window: files, the library, tabs, notation editing, comments, headers, clipboard.
// Everything happens in a temporary folder; background work is awaited through signals.

#include "MainWindow.hpp"
#include "board/BoardView.hpp"
#include "kiebitz/pgn/pgn.hpp"
#include "library/Library.hpp"
#include "library/LibraryView.hpp"
#include "library/NewFileDialog.hpp"
#include "notation/NotationView.hpp"
#include "panels/GameInfoPanel.hpp"
#include "repertoire/GapSource.hpp"
#include "repertoire/RepertoirePanel.hpp"
#include "repertoire/RepertoireStore.hpp"
#include "style/AppStyle.hpp"
#include "widgets/DateRange.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QComboBox>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

namespace {

using kiebitz::app::AppStyle;
using kiebitz::app::BoardView;
using kiebitz::app::Library;
using kiebitz::app::MainWindow;
using kiebitz::app::NotationView;
using namespace kiebitz;

constexpr const char* kOneGame = R"([Event "Club"]
[White "Alice"]
[Black "Bob"]
[Result "*"]

1. e4 e5 2. Nf3 *
)";

constexpr const char* kThreeGames = R"([Event "A"]
[White "First"]
[Black "X"]
[Result "1-0"]

1. d4 d5 1-0

[Event "B"]
[White "Second"]
[Black "Y"]
[Result "*"]

1. c4 e5 2. Nc3 *

[Event "C"]
[White "Third"]
[Black "Z"]
[Result "0-1"]

1. Nf3 d5 0-1
)";

void writeFile(const QString& path, const char* text) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(text);
}

std::string readFile(const QString& path) {
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return file.readAll().toStdString();
}

QAction* action(MainWindow& window, const QString& text) {
    for (auto* candidate : window.findChildren<QAction*>()) {
        if (candidate->text().remove(QLatin1Char('&')) == text) {
            return candidate;
        }
    }
    return nullptr;
}

class DocumentsTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(folder.isValid());
        window.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
        board = window.findChild<BoardView*>(QStringLiteral("gameBoard"));
        tabs = window.findChild<QTabBar*>(QStringLiteral("documentTabs"));
        notation = window.findChild<NotationView*>();
        ASSERT_NE(board, nullptr);
        ASSERT_NE(tabs, nullptr);
        ASSERT_NE(notation, nullptr);
    }

    QString path(const char* name) const { return folder.filePath(QString::fromUtf8(name)); }
    [[nodiscard]] std::string fen() const { return board->position().fen(); }
    void play(std::string_view san) {
        emit board->moveMade(board->position().parseSan(san).value());
    }
    /// Opens a file and waits until its game is showing.
    void open(const QString& file) {
        QSignalSpy opened(&window, &MainWindow::fileOpened);
        window.openFile(file);
        ASSERT_TRUE(opened.wait(5000));
    }
    void saveAndWait() {
        QSignalSpy saved(&window, &MainWindow::fileSaved);
        action(window, QStringLiteral("Save"))->trigger();
        ASSERT_TRUE(saved.wait(5000));
    }
    void scanLibrary() {
        QSignalSpy scanned(&window.library(), &Library::scanned);
        window.library().setFolder(folder.path());
        ASSERT_TRUE(scanned.wait(5000));
    }

    QTemporaryDir folder;
    AppStyle style{*qApp};
    MainWindow window{style};
    BoardView* board = nullptr;
    QTabBar* tabs = nullptr;
    NotationView* notation = nullptr;
};

TEST_F(DocumentsTest, StartsInTheLibraryWithAnEmptyGameTab) {
    EXPECT_EQ(window.section(), MainWindow::Section::Library);
    EXPECT_EQ(tabs->count(), 1); // the board's tab bar holds only games
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("New game"));
}

TEST_F(DocumentsTest, TheSidebarSwitchesSectionsAndTheBoardAlwaysHasAGame) {
    auto* board = window.findChild<QToolButton*>(QStringLiteral("sideBoard"));
    auto* engines = window.findChild<QToolButton*>(QStringLiteral("sideEngines"));
    ASSERT_NE(board, nullptr);
    ASSERT_NE(engines, nullptr);
    QTest::mouseClick(engines, Qt::LeftButton);
    EXPECT_EQ(window.section(), MainWindow::Section::Engines);
    EXPECT_TRUE(window.findChild<QWidget*>(QStringLiteral("enginesView"))->isVisible());
    QTest::mouseClick(board, Qt::LeftButton);
    EXPECT_EQ(window.section(), MainWindow::Section::Board);
    EXPECT_TRUE(board->isChecked());
    // Closing the last game leaves a fresh one, never an empty board page.
    action(window, QStringLiteral("Close Tab"))->trigger();
    EXPECT_EQ(tabs->count(), 1);
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("New game"));
}

TEST_F(DocumentsTest, QuittingNeverAsksAndSavesGamesFromFiles) {
    // A changed game from a file, and a changed new game in no file.
    writeFile(path("game.pgn"), kOneGame);
    open(path("game.pgn"));
    action(window, QStringLiteral("Go to End"))->trigger();
    play("Nc6");
    action(window, QStringLiteral("New Tab"))->trigger();
    play("d4");
    // Quitting: no question (it would block this test); the file's game is saved.
    EXPECT_TRUE(window.close());
    const auto saved = pgn::parseGame(readFile(path("game.pgn")));
    ASSERT_TRUE(saved.ok());
    EXPECT_EQ(saved->mainLine().size(), 5U); // root + e4 e5 Nf3 Nc6
}

TEST_F(DocumentsTest, ClosingATabNeverAsks) {
    // A new game with moves, in no file: it just closes (a question would block this test).
    play("e4");
    action(window, QStringLiteral("Close Tab"))->trigger();
    EXPECT_EQ(tabs->count(), 1);
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("New game"));
}

TEST_F(DocumentsTest, ClocksShowAboveAndBelowTheBoardWithoutMovingIt) {
    writeFile(path("plain.pgn"), kOneGame);
    writeFile(path("clocks.pgn"), "[White \"Alice\"]\n[Black \"Bob\"]\n\n"
                                  "1. e4 { [%clk 0:05:00] } e5 { [%clk 0:04:58] } "
                                  "2. Nf3 { [%clk 0:04:51] } *\n");
    auto* top = window.findChild<QLabel*>(QStringLiteral("clockTop"));
    auto* bottom = window.findChild<QLabel*>(QStringLiteral("clockBottom"));
    ASSERT_NE(top, nullptr);
    ASSERT_NE(bottom, nullptr);

    open(path("plain.pgn"));
    action(window, QStringLiteral("Go to End"))->trigger();
    QApplication::processEvents();
    const QRect squares = board->squareRect(core::Square::at(0, 7));
    EXPECT_FALSE(top->isVisible());
    EXPECT_FALSE(bottom->isVisible());

    open(path("clocks.pgn"));
    action(window, QStringLiteral("Go to End"))->trigger();
    QApplication::processEvents();
    EXPECT_EQ(bottom->text(), QStringLiteral("4:51")); // White, at the bottom
    EXPECT_EQ(top->text(), QStringLiteral("4:58"));
    EXPECT_TRUE(top->isVisible());
    EXPECT_TRUE(bottom->isVisible());
    EXPECT_EQ(board->squareRect(core::Square::at(0, 7)), squares); // the board didn't move
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        window.grab().save(QDir(dir).filePath(QStringLiteral("clocks.png")));
    }
    // Back one move: White's clock is the one after 1. e4.
    action(window, QStringLiteral("Back"))->trigger();
    EXPECT_EQ(bottom->text(), QStringLiteral("5:00"));
    // Flipped: Black at the bottom.
    action(window, QStringLiteral("Flip Board"))->trigger();
    EXPECT_EQ(bottom->text(), QStringLiteral("4:58"));
    action(window, QStringLiteral("Flip Board"))->trigger();
}

TEST_F(DocumentsTest, OpenEditAndSaveAFile) {
    writeFile(path("game.pgn"), kOneGame);
    open(path("game.pgn"));
    EXPECT_EQ(tabs->tabText(tabs->currentIndex()), QStringLiteral("Alice – Bob"));
    EXPECT_EQ(tabs->count(), 1); // replaced the untouched empty game

    action(window, QStringLiteral("Go to End"))->trigger();
    play("Nc6");
    EXPECT_EQ(tabs->tabText(tabs->currentIndex()), QStringLiteral("• Alice – Bob"));

    saveAndWait();
    EXPECT_EQ(tabs->tabText(tabs->currentIndex()), QStringLiteral("Alice – Bob"));
    const auto saved = pgn::parseGame(readFile(path("game.pgn")));
    ASSERT_TRUE(saved.ok());
    EXPECT_EQ(saved->mainLine().size(), 5U); // root + e4 e5 Nf3 Nc6
    EXPECT_EQ(saved->header("White"), "Alice");
}

TEST_F(DocumentsTest, LibraryListsFilesAndOpensAnyGame) {
    writeFile(path("one.pgn"), kOneGame);
    writeFile(path("three.pgn"), kThreeGames);
    scanLibrary();
    ASSERT_EQ(window.library().files().size(), 2U);
    const auto* files = window.findChild<QTreeWidget*>(QStringLiteral("libraryFiles"));
    ASSERT_NE(files, nullptr);
    EXPECT_EQ(files->topLevelItemCount(), 2);

    const auto& three = window.library().files()[1];
    ASSERT_EQ(three.pgn->games.size(), 3U);
    QSignalSpy opened(&window, &MainWindow::fileOpened);
    window.openGame(three.pgn, 1);
    ASSERT_TRUE(opened.wait(5000));
    EXPECT_EQ(tabs->tabText(tabs->currentIndex()), QStringLiteral("Second – Y"));

    // Opening the same game again switches to its tab instead of opening another.
    const int count = tabs->count();
    window.showSection(MainWindow::Section::Library);
    window.openGame(three.pgn, 1);
    EXPECT_EQ(tabs->count(), count);
    EXPECT_EQ(tabs->tabText(tabs->currentIndex()), QStringLiteral("Second – Y"));
}

/// A folder tree like a real library: OTB/White, OTB/Black, and a file at the top.
class LibraryTreeTest : public DocumentsTest {
protected:
    void SetUp() override {
        DocumentsTest::SetUp();
        QDir(folder.path()).mkpath(QStringLiteral("OTB/White"));
        QDir(folder.path()).mkpath(QStringLiteral("OTB/Black"));
        writeFile(path("OTB/White/a.pgn"), kOneGame);
        writeFile(path("OTB/Black/b.pgn"), kThreeGames);
        writeFile(path("top.pgn"), kOneGame);
        QSettings().remove(QStringLiteral("library/expandedFolders"));
        QSettings().remove(QStringLiteral("library/foldingSaved"));
        scanLibrary();
        tree = window.findChild<QTreeWidget*>(QStringLiteral("libraryFiles"));
        games = window.findChild<QTableView*>(QStringLiteral("libraryGames"));
        ASSERT_NE(tree, nullptr);
        ASSERT_NE(games, nullptr);
        window.showSection(MainWindow::Section::Library);
    }
    [[nodiscard]] QTreeWidgetItem* item(const QString& text) const {
        const auto found = tree->findItems(text, Qt::MatchExactly | Qt::MatchRecursive);
        return found.isEmpty() ? nullptr : found.front();
    }
    QTreeWidget* tree = nullptr;
    QTableView* games = nullptr;
};

TEST_F(LibraryTreeTest, AFolderListsOnlyItsOwnFiles) {
    ASSERT_EQ(tree->topLevelItemCount(), 2); // the OTB folder, then top.pgn
    QTreeWidgetItem* otb = item(QStringLiteral("OTB"));
    ASSERT_NE(otb, nullptr);
    EXPECT_EQ(otb->childCount(), 2); // Black, White
    // A folder shows what's in it, as En Croissant does; a file its games.
    auto* detail = window.findChild<QStackedWidget*>(QStringLiteral("libraryDetail"));
    ASSERT_NE(detail, nullptr);
    const auto shown = [&] {
        QStringList texts;
        for (const auto* label : detail->currentWidget()->findChildren<QLabel*>()) {
            if (!label->text().isEmpty()) {
                texts << label->text();
            }
        }
        return texts;
    };
    tree->setCurrentItem(otb);
    EXPECT_EQ(shown(),
              (QStringList{QStringLiteral("OTB"), QStringLiteral("2 folders  ·  0 files")}));
    tree->setCurrentItem(item(QStringLiteral("b")));
    EXPECT_EQ(games->model()->rowCount(), 3); // b.pgn's games
    const auto* count = window.findChild<QLabel*>(QStringLiteral("libraryListCount"));
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->text(), QStringLiteral("Games  ·  3 games"));
    writeFile(path("OTB/notes.pgn"), kOneGame);
    QSignalSpy scanned(&window.library(), &Library::scanned);
    ASSERT_TRUE(scanned.wait(5000));
    tree->setCurrentItem(item(QStringLiteral("OTB")));
    EXPECT_EQ(shown(),
              (QStringList{QStringLiteral("OTB"), QStringLiteral("2 folders  ·  1 file")}));
}

TEST_F(LibraryTreeTest, NewFoldersAndMovingFiles) {
    auto* view = window.findChild<kiebitz::app::LibraryView*>();
    ASSERT_NE(view, nullptr);
    // A new folder shows even though it is empty, and is selected.
    QSignalSpy scanned(&window.library(), &Library::scanned);
    EXPECT_EQ(view->createFolder(QStringLiteral("OTB"), QStringLiteral("Blitz")), QString());
    ASSERT_TRUE(scanned.wait(5000));
    ASSERT_NE(item(QStringLiteral("Blitz")), nullptr);
    EXPECT_EQ(item(QStringLiteral("Blitz"))->parent(), item(QStringLiteral("OTB")));
    EXPECT_EQ(tree->currentItem(), item(QStringLiteral("Blitz")));
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        window.resize(1200, 760);
        QTest::qWait(100);
        tree->window()->grab().save(QDir(dir).filePath(QStringLiteral("library-tree.png")));
    }
    EXPECT_NE(view->createFolder(QStringLiteral("OTB"), QStringLiteral("Blitz")), QString());

    // Open top.pgn, then move it into the new folder: the tab saves to its new place.
    QSignalSpy opened(&window, &MainWindow::fileOpened);
    window.openFile(path("top.pgn"));
    ASSERT_TRUE(opened.wait(5000));
    EXPECT_EQ(view->moveToFolder({path("top.pgn")}, QStringLiteral("OTB/Blitz")), QString());
    EXPECT_TRUE(QFileInfo::exists(path("OTB/Blitz/top.pgn")));
    EXPECT_FALSE(QFileInfo::exists(path("top.pgn")));
    ASSERT_TRUE(scanned.wait(5000));
    EXPECT_EQ(item(QStringLiteral("top"))->parent(), item(QStringLiteral("Blitz")));
    play("a3"); // an edit saves itself, into the moved file
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return readFile(path("OTB/Blitz/top.pgn")).find("a3") != std::string::npos; }, 5000));
    EXPECT_FALSE(QFileInfo::exists(path("top.pgn")));

    // A folder moves with what's in it, but never into itself.
    EXPECT_NE(view->moveToFolder({path("OTB")}, QStringLiteral("OTB/Blitz")), QString());
    EXPECT_EQ(view->moveToFolder({path("OTB/Blitz")}, QString()), QString());
    EXPECT_TRUE(QFileInfo::exists(path("Blitz/top.pgn")));
    ASSERT_TRUE(scanned.wait(5000));

    // Rename: a file keeps its .pgn; the open tab follows; a taken name is refused.
    EXPECT_EQ(view->rename(path("Blitz/top.pgn"), QStringLiteral("Club")), QString());
    EXPECT_TRUE(QFileInfo::exists(path("Blitz/Club.pgn")));
    ASSERT_TRUE(scanned.wait(5000));
    EXPECT_NE(item(QStringLiteral("Club")), nullptr);
    EXPECT_EQ(tree->currentItem(), item(QStringLiteral("Club")));
    EXPECT_NE(view->rename(path("Blitz/Club.pgn"), QStringLiteral("a/b")), QString());
    const bool whiteToMove = fen().find(" w ") != std::string::npos;
    play(whiteToMove ? "h3" : "h6");
    ASSERT_TRUE(QTest::qWaitFor(
        [&] {
            return readFile(path("Blitz/Club.pgn")).find(whiteToMove ? "h3" : "h6") !=
                   std::string::npos;
        },
        5000));

    // Move to Trash (here: deleted, to keep the real Trash clean): its tab closes.
    view->setTrashForTests([](const QString& gone) { return QFile::remove(gone); });
    const int tabsBefore = tabs->count();
    EXPECT_EQ(view->moveToTrash(path("Blitz/Club.pgn")), QString());
    EXPECT_FALSE(QFileInfo::exists(path("Blitz/Club.pgn")));
    EXPECT_EQ(tabs->count(), std::max(tabsBefore - 1, 1));
    QTest::qWait(1500); // no autosave brings it back
    EXPECT_FALSE(QFileInfo::exists(path("Blitz/Club.pgn")));
}

TEST_F(LibraryTreeTest, FoldingIsRemembered) {
    QTreeWidgetItem* otb = item(QStringLiteral("OTB"));
    ASSERT_TRUE(otb->isExpanded()); // top-level folders start open
    otb->setExpanded(false);
    QSignalSpy scanned(&window.library(), &Library::scanned);
    writeFile(path("another.pgn"), kOneGame); // a new file: the tree is rebuilt
    ASSERT_TRUE(scanned.wait(5000));
    EXPECT_FALSE(item(QStringLiteral("OTB"))->isExpanded());
}

TEST_F(LibraryTreeTest, ChoosingAnotherFolderShowsItsFiles) {
    const QTemporaryDir other;
    ASSERT_TRUE(other.isValid());
    writeFile(other.filePath(QStringLiteral("elsewhere.pgn")), kThreeGames);
    QSignalSpy scanned(&window.library(), &Library::scanned);
    // The macOS folder panel hands back the path with a slash at the end.
    window.library().setFolder(other.path() + QLatin1Char('/'));
    ASSERT_TRUE(scanned.wait(5000));

    EXPECT_EQ(tree->topLevelItemCount(), 1);
    EXPECT_NE(item(QStringLiteral("elsewhere")), nullptr);
    EXPECT_EQ(item(QStringLiteral("top")), nullptr); // the old folder's files are gone
    const auto* button = window.findChild<QPushButton*>(QStringLiteral("libraryFolder"));
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->text(), QFileInfo(other.path()).fileName()); // the folder's name, no path
}

TEST_F(LibraryTreeTest, AQuietFolderIsScannedOnceAndKeepsTheSelection) {
    tree->setCurrentItem(item(QStringLiteral("b")));
    games->setCurrentIndex(games->model()->index(2, 1));
    QSignalSpy scanned(&window.library(), &Library::scanned);
    // Something that isn't a PGN file changes: the folder is looked at again, but nothing is
    // rebuilt, so the selection stays where it was.
    writeFile(path("OTB/notes.txt"), "not a game");
    QTest::qWait(1200);
    EXPECT_EQ(scanned.size(), 0);
    EXPECT_EQ(tree->currentItem()->text(0), QStringLiteral("b"));
    EXPECT_EQ(games->currentIndex().row(), 2);

    // A real change rebuilds the lists; the selected file and game are found again.
    writeFile(path("new.pgn"), kOneGame);
    ASSERT_TRUE(scanned.wait(5000));
    EXPECT_EQ(tree->currentItem()->text(0), QStringLiteral("b"));
    EXPECT_EQ(games->currentIndex().row(), 2);
}

TEST_F(LibraryTreeTest, AGameWithoutPlayersIsNamedAfterItsFile) {
    QSignalSpy scanned(&window.library(), &Library::scanned);
    // As En Croissant writes its opening files: every header a "?".
    writeFile(path("Bird Opening.pgn"),
              "[Event \"?\"]\n[White \"?\"]\n[Black \"?\"]\n\n1. f4 d5 *\n");
    ASSERT_TRUE(scanned.wait(5000));
    tree->setCurrentItem(item(QStringLiteral("Bird Opening")));
    // The White column has the file's name (never a blank row); no "?" for Black.
    EXPECT_EQ(games->model()->index(0, 1).data().toString(), QStringLiteral("Bird Opening"));
    EXPECT_EQ(games->model()->index(0, 2).data().toString(), QString());
    tree->setCurrentItem(item(QStringLiteral("top"))); // a game with players shows them
    EXPECT_EQ(games->model()->index(0, 1).data().toString(), QStringLiteral("Alice"));
    tree->setCurrentItem(item(QStringLiteral("Bird Opening")));
    const auto* players = window.findChild<QLabel*>(QStringLiteral("previewPlayers"));
    ASSERT_NE(players, nullptr);
    EXPECT_EQ(players->text(), QStringLiteral("Bird Opening"));
}

TEST_F(LibraryTreeTest, ALongFileNameDoesNotWidenTheWindow) {
    // Measure the layouts as they are now (layouts cache their minimum sizes).
    const auto minimumWidth = [&] {
        for (auto* layout : window.findChildren<QLayout*>()) {
            layout->invalidate();
        }
        return window.minimumSizeHint().width();
    };
    const QString name = QStringLiteral("Symmetrical English - 2.g3 Nc6, Early d4 and b-pawn "
                                        "systems - after 5.a3 + after 5.Nf3 - Training version");
    // No players, as in an opening file: the tab is named after the file too. The same game
    // in a file with a short name, to compare against.
    writeFile(folder.filePath(QStringLiteral("Short.pgn")),
              "[White \"?\"]\n[Black \"?\"]\n\n1. e4 *\n");
    writeFile(folder.filePath(name + QStringLiteral(".pgn")),
              "[White \"?\"]\n[Black \"?\"]\n\n1. e4 *\n");
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return item(name) != nullptr && item(QStringLiteral("Short")) != nullptr; }, 5000));
    // Selected and shown (a rescan for the new files may come in between: select again then).
    const auto* title = window.findChild<QLabel*>(QStringLiteral("libraryListTitle"));
    ASSERT_NE(title, nullptr);
    const auto show = [&](const QString& file) {
        return QTest::qWaitFor(
            [&] {
                if (title->toolTip() != file && item(file) != nullptr) {
                    tree->setCurrentItem(item(file));
                }
                return title->toolTip() == file;
            },
            10000);
    };
    ASSERT_TRUE(show(QStringLiteral("Short")));
    const int before = minimumWidth();
    ASSERT_TRUE(show(name));
    // The heading over the game list is cut off with "…" (the tooltip has all of it) ...
    EXPECT_TRUE(title->text().endsWith(QStringLiteral("…")));
    EXPECT_EQ(title->toolTip(), name);
    EXPECT_LE(minimumWidth(), before);

    // ... and so is the tab of a game opened from it, so the tab bar is never squeezed.
    QSignalSpy opened(&window, &MainWindow::fileOpened);
    window.openFile(folder.filePath(name + QStringLiteral(".pgn")));
    ASSERT_TRUE(opened.wait(5000));
    QApplication::processEvents();
    EXPECT_GE(tabs->width(), tabs->sizeHint().width());
    EXPECT_LT(tabs->sizeHint().width(), 500);
    EXPECT_TRUE(tabs->tabText(tabs->currentIndex()).endsWith(QStringLiteral("…")));
    EXPECT_EQ(tabs->tabToolTip(tabs->currentIndex()), name);
}

TEST_F(LibraryTreeTest, ArrowKeysActOnWhatWasClicked) {
    const std::string before = fen(); // the game tab's board must not move
    tree->setFocus();
    tree->setCurrentItem(item(QStringLiteral("top")));
    QTest::keyClick(tree, Qt::Key_Up); // up the tree, not through a game
    EXPECT_NE(tree->currentItem()->text(0), QStringLiteral("top"));

    // → on a file stays in the tree: the keys never change column by themselves.
    tree->setCurrentItem(item(QStringLiteral("b")));
    QTest::keyClick(tree, Qt::Key_Right);
    EXPECT_TRUE(tree->hasFocus());

    // Click a game: ↑ ↓ scroll through the games.
    const auto* preview = window.findChild<QLabel*>(QStringLiteral("previewMove"));
    ASSERT_NE(preview, nullptr);
    const QRect first = games->visualRect(games->model()->index(0, 1));
    QTest::mouseClick(games->viewport(), Qt::LeftButton, {}, first.center());
    EXPECT_TRUE(games->hasFocus());
    QTest::keyClick(games, Qt::Key_Down); // the second game: 1. c4 e5 2. Nc3
    EXPECT_EQ(games->currentIndex().row(), 1);
    // The preview parses the game in the background: wait for it.
    QTRY_COMPARE(preview->text(), QStringLiteral("Final position, after 2. Nc3"));
    // ← → there play through the previewed game, and the list keeps the keys.
    QTest::keyClick(games, Qt::Key_Left);
    EXPECT_EQ(preview->text(), QStringLiteral("After 1… e5"));
    QTest::keyClick(games, Qt::Key_Right);
    EXPECT_EQ(preview->text(), QStringLiteral("Final position, after 2. Nc3"));
    EXPECT_TRUE(games->hasFocus());

    // Click a step button under the preview: now the keys belong to the preview (outlined).
    auto* board = window.findChild<kiebitz::app::BoardView*>(QStringLiteral("previewBoard"));
    ASSERT_NE(board, nullptr);
    auto* back = window.findChild<QPushButton*>(QStringLiteral("previewBack"));
    ASSERT_NE(back, nullptr);
    QTest::mouseClick(back, Qt::LeftButton);
    EXPECT_EQ(preview->text(), QStringLiteral("After 1… e5"));
    EXPECT_TRUE(board->hasFocus());
    const auto* detailCard =
        window.findChild<QStackedWidget*>(QStringLiteral("libraryDetail"))->parentWidget();
    EXPECT_TRUE(detailCard->property("focused").toBool());
    QTest::keyClick(board, Qt::Key_Left);
    EXPECT_EQ(preview->text(), QStringLiteral("After 1. c4"));
    QTest::keyClick(board, Qt::Key_Left);
    EXPECT_EQ(preview->text(), QStringLiteral("Start position"));
    QTest::keyClick(board, Qt::Key_Left); // at the start: stays put, and stays in the preview
    EXPECT_EQ(preview->text(), QStringLiteral("Start position"));
    EXPECT_TRUE(board->hasFocus());
    QTest::keyClick(board, Qt::Key_Down); // ↓: the end, not another game
    EXPECT_EQ(preview->text(), QStringLiteral("Final position, after 2. Nc3"));
    EXPECT_EQ(games->currentIndex().row(), 1);
    QTest::keyClick(board, Qt::Key_Up);
    EXPECT_EQ(preview->text(), QStringLiteral("Start position"));
    EXPECT_EQ(fen(), before); // the game tab never moved
}

TEST_F(DocumentsTest, SavingOneGameLeavesTheOthersAlone) {
    writeFile(path("three.pgn"), kThreeGames);
    scanLibrary();
    EXPECT_FALSE(window.library().files()[0].pgn->hasText); // the Library keeps only the index
    QSignalSpy opened(&window, &MainWindow::fileOpened);
    window.openGame(window.library().files()[0].pgn, 1);
    ASSERT_TRUE(opened.wait(5000));
    action(window, QStringLiteral("Go to End"))->trigger();
    play("Nc6");
    saveAndWait();

    const auto games = pgn::parseAll(readFile(path("three.pgn")));
    ASSERT_TRUE(games.ok()) << games.error().message;
    ASSERT_EQ(games->size(), 3U);
    EXPECT_EQ((*games)[0].mainLine().size(), 3U); // untouched
    EXPECT_EQ((*games)[1].mainLine().size(), 5U); // 1. c4 e5 2. Nc3 Nc6
    EXPECT_EQ((*games)[2].header("White"), "Third");
}

/// Answers the next message box with `button` (it blocks the test otherwise).
/// Answers the next message the app shows (its own dialog): its action, or Cancel / OK.
void answerNextMessage(bool act) {
    QTimer::singleShot(0, [act] {
        for (const QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() == u"messageDialog" && widget->isVisible()) {
                auto* button = widget->findChild<QPushButton*>(
                    act ? QStringLiteral("messageAction") : QStringLiteral("messageCancel"));
                if (button == nullptr) {
                    button = widget->findChild<QPushButton*>(QStringLiteral("messageOk"));
                }
                button->click();
                return;
            }
        }
    });
}

TEST_F(DocumentsTest, FileTypesShowInTheLibraryAndFilterIt) {
    writeFile(path("najdorf.pgn"), "[FileType \"Repertoire\"]\n[Orientation \"black\"]\n\n"
                                   "1. e4 c5 *\n");
    writeFile(path("london.pgn"), "[FileType \"Repertoire\"]\n[Orientation \"white\"]\n\n"
                                  "1. d4 d5 2. Bf4 *\n");
    writeFile(path("tactics.pgn"), "[FileType \"Puzzle\"]\n\n1. e4 *\n");
    writeFile(path("games.pgn"), kOneGame);
    scanLibrary();
    window.showSection(MainWindow::Section::Library);
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("libraryFiles"));
    ASSERT_NE(tree, nullptr);
    ASSERT_EQ(tree->topLevelItemCount(), 4); // each file once, where it is
    std::map<QString, QTreeWidgetItem*> items;
    for (int i = 0; i < 4; ++i) {
        items[tree->topLevelItem(i)->text(0)] = tree->topLevelItem(i);
    }
    // Each type its own icon (a repertoire's side is in its tooltip, not its icon).
    QSet<QByteArray> icons; // the icons' pixels
    for (const auto& [name, item] : items) {
        QByteArray pixels;
        QBuffer buffer(&pixels);
        buffer.open(QIODevice::WriteOnly);
        item->icon(0).pixmap(16, 16).save(&buffer, "PNG");
        icons.insert(pixels);
    }
    EXPECT_EQ(icons.size(), 3);
    EXPECT_TRUE(items[QStringLiteral("najdorf")]->toolTip(0).endsWith(u"Repertoire for Black"));

    // The type filter: only repertoires.
    auto* filter = window.findChild<QComboBox*>(QStringLiteral("libraryTypeFilter"));
    ASSERT_NE(filter, nullptr);
    filter->setCurrentIndex(
        filter->findData(static_cast<int>(kiebitz::library::FileKind::Repertoire)));
    EXPECT_FALSE(items[QStringLiteral("najdorf")]->isHidden());
    EXPECT_FALSE(items[QStringLiteral("london")]->isHidden());
    EXPECT_TRUE(items[QStringLiteral("games")]->isHidden());
    EXPECT_TRUE(items[QStringLiteral("tactics")]->isHidden());
    // Back to all files: the tree is rebuilt, so items are looked up again from here on.
    const auto find = [&](const QString& name) {
        return tree->findItems(name, Qt::MatchExactly | Qt::MatchRecursive).value(0);
    };
    filter->setCurrentIndex(0);
    EXPECT_FALSE(find(QStringLiteral("games"))->isHidden());
    // The search finds names, and what's inside the files: players, events, openings.
    auto* search = window.findChild<QLineEdit*>(QStringLiteral("libraryFileSearch"));
    ASSERT_NE(search, nullptr);
    search->setText(QStringLiteral("LON"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return find(QStringLiteral("najdorf"))->isHidden(); }, 2000));
    EXPECT_FALSE(find(QStringLiteral("london"))->isHidden());
    search->setText(QStringLiteral("alice")); // a player in games.pgn
    ASSERT_TRUE(QTest::qWaitFor([&] { return find(QStringLiteral("london"))->isHidden(); }, 2000));
    EXPECT_FALSE(find(QStringLiteral("games"))->isHidden());
    tree->setCurrentItem(find(QStringLiteral("games"))); // its games are searched for it too
    EXPECT_EQ(window.findChild<QLineEdit*>(QStringLiteral("librarySearchGames"))->text(),
              QStringLiteral("alice"));
    search->clear();
    ASSERT_TRUE(QTest::qWaitFor([&] { return !find(QStringLiteral("london"))->isHidden(); }, 2000));
    EXPECT_FALSE(find(QStringLiteral("najdorf"))->isHidden());

    // A puzzle file: who is to move, not White and Black.
    tree->setCurrentItem(find(QStringLiteral("tactics")));
    auto* games = window.findChild<QTableView*>(QStringLiteral("libraryGames"));
    ASSERT_NE(games, nullptr);
    EXPECT_EQ(games->model()->headerData(2, Qt::Horizontal).toString(), QStringLiteral("TO MOVE"));
    EXPECT_EQ(games->model()->index(0, 2).data().toString(), QStringLiteral("White"));

    // A repertoire file: its overview instead of a games table.
    auto* view = window.findChild<kiebitz::app::LibraryView*>();
    ASSERT_NE(view, nullptr);
    tree->setCurrentItem(find(QStringLiteral("najdorf")));
    ASSERT_TRUE(
        QTest::qWaitFor([&] { return view->repertoireStats().value(0) == u"1"; }, 5000)); // 1... c5
    EXPECT_EQ(view->repertoireStats(),
              (QStringList{QStringLiteral("1"), QStringLiteral("1"), QStringLiteral("move 1")}));
    // The preview: from the start (one line: the file starts there), named by the file until
    // a move names the opening; the names load in the background (slow under ASan).
    const auto* name = window.findChild<QLabel*>(QStringLiteral("repertoireName"));
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->text(), QStringLiteral("najdorf"));
    auto* board =
        window.findChild<kiebitz::app::BoardView*>(QStringLiteral("repertoireStartBoard"));
    QTest::mouseClick(board, Qt::LeftButton);
    QTest::keyClick(board, Qt::Key_End);
    ASSERT_TRUE(QTest::qWaitFor([&] { return name->text() == u"Sicilian Defense"; }, 20000));
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        window.resize(1200, 760);
        QTest::qWait(100);
        window.grab().save(QDir(dir).filePath(QStringLiteral("library-repertoire.png")));
        tree->setCurrentItem(find(QStringLiteral("games")));
        QTest::qWait(300);
        window.grab().save(QDir(dir).filePath(QStringLiteral("library-games.png")));
        filter->showPopup(); // the dropdown's list, on its own
        QTest::qWait(200);
        filter->view()->window()->grab().save(QDir(dir).filePath(QStringLiteral("dropdown.png")));
        filter->hidePopup();
        QMenu menu; // how menus look: a highlighted row, a tick on the chosen one
        menu.addAction(QStringLiteral("Open"));
        menu.addSeparator();
        auto* chosen = menu.addAction(QStringLiteral("Repertoire for Black"));
        chosen->setCheckable(true);
        chosen->setChecked(true);
        menu.addAction(QStringLiteral("Tournament"))->setCheckable(true);
        menu.popup(QPoint(100, 100));
        menu.setActiveAction(chosen);
        QTest::qWait(200);
        menu.grab().save(QDir(dir).filePath(QStringLiteral("menu.png")));
        menu.hide();
    }
}

TEST_F(DocumentsTest, SettingTheTypeOfAFolder) {
    QDir(folder.path()).mkpath(QStringLiteral("Black Solid"));
    writeFile(path("Black Solid/QGA.pgn"), "1. d4 d5 2. c4 dxc4 *\n");
    writeFile(path("Black Solid/Sicilian.pgn"), "1. e4 c5 2. Nf3 Nc6 *\n");
    scanLibrary();
    auto& store = window.repertoires();
    const QStringList paths{path("Black Solid/QGA.pgn"), path("Black Solid/Sicilian.pgn")};
    emit window.findChild<kiebitz::app::LibraryView*>()->fileKindRequested(
        paths, static_cast<int>(kiebitz::library::FileKind::Repertoire), 1);
    ASSERT_TRUE(QTest::qWaitFor([&] { return store.files(core::Color::Black).size() == 2; }, 5000));
    for (const QString& file : paths) {
        const auto games = pgn::indexGames(readFile(file));
        EXPECT_EQ(games.front().header("FileType"), "Repertoire");
        EXPECT_EQ(games.front().header("Orientation"), "black");
    }
    // Back to games: the type changes, the side header stays.
    emit window.findChild<kiebitz::app::LibraryView*>()->fileKindRequested(
        paths, static_cast<int>(kiebitz::library::FileKind::Game), -1);
    ASSERT_TRUE(QTest::qWaitFor([&] { return store.files(core::Color::Black).isEmpty(); }, 5000));
}

TEST_F(DocumentsTest, RepertoireFileOpensWithItsTreeFromItsSide) {
    writeFile(
        path("najdorf.pgn"),
        "[Event \"Najdorf\"]\n[FileType \"Repertoire\"]\n[Orientation \"black\"]\n\n"
        "1. e4 c5 2. Nf3 d6 { The Najdorf move order } 3. d4 (3. Bb5+ { Rossolimo-like } Bd7) "
        "cxd4 4. Nxd4 Nf6 5. Nc3 a6 *\n");
    writeFile(path("games.pgn"), kOneGame);
    scanLibrary();
    open(path("najdorf.pgn"));
    const QString text = notation->plainText();
    EXPECT_TRUE(text.contains(QStringLiteral("The Najdorf move order")));
    EXPECT_TRUE(text.contains(QStringLiteral("Rossolimo-like")));
    EXPECT_TRUE(board->isFlipped());
    // The Repertoire tab is about practice here.
    auto* panel = window.findChild<kiebitz::app::RepertoirePanel*>();
    ASSERT_NE(panel, nullptr);
    EXPECT_TRUE(panel->practiceText().contains(QStringLiteral("positions to practice")));
    // Info shows what a repertoire has: no players, but the side it is for.
    auto* white = window.findChild<QLineEdit*>(QStringLiteral("headerWhite"));
    auto* side = window.findChild<QComboBox*>(QStringLiteral("headerOrientation"));
    ASSERT_NE(white, nullptr);
    ASSERT_NE(side, nullptr);
    EXPECT_TRUE(white->isHidden());
    EXPECT_FALSE(side->isHidden());
    EXPECT_EQ(side->currentText(), QStringLiteral("Black"));
    side->setCurrentIndex(0);
    emit side->activated(0); // For: White, and the board turns
    EXPECT_FALSE(board->isFlipped());
    // A game without [Orientation] goes back to the side chosen before, and has players.
    open(path("games.pgn"));
    EXPECT_FALSE(board->isFlipped());
    EXPECT_FALSE(white->isHidden());
    EXPECT_TRUE(side->isHidden());
}

TEST_F(DocumentsTest, GamesAreCheckedAgainstEveryRepertoireFile) {
    writeFile(path("Open Games.pgn"), "[FileType \"Repertoire\"]\n[Orientation \"white\"]\n\n"
                                      "1. e4 e5 2. Nf3 Nc6 3. Bb5 *\n");
    writeFile(path("Queens Gambit.pgn"), "[FileType \"Repertoire\"]\n[Orientation \"white\"]\n\n"
                                         "1. d4 d5 2. c4 *\n");
    writeFile(path("game.pgn"), "[White \"Me\"]\n[Black \"You\"]\n\n1. e4 e5 2. Nf3 d6 3. d4 *\n");
    scanLibrary();
    auto& store = window.repertoires();
    ASSERT_TRUE(QTest::qWaitFor([&] { return store.get(core::Color::White) != nullptr; }, 5000));
    auto* panel = window.findChild<kiebitz::app::RepertoirePanel*>();
    ASSERT_NE(panel, nullptr);

    open(path("game.pgn"));
    action(window, QStringLiteral("Go to Start"))->trigger();
    play("e4"); // the tab follows the current move: a new variation here is fine
    action(window, QStringLiteral("Go to End"))->trigger();
    EXPECT_EQ(panel->statusText(core::Color::White),
              QStringLiteral("2… d6 isn't covered yet · Open Games"));
    emit panel->addLineRequested(core::Color::White);
    // The write runs in the background: wait for it (slow under a full parallel test run).
    ASSERT_TRUE(QTest::qWaitFor(
        [&] {
            return panel->statusText(core::Color::White).startsWith(u"In book · Open Games") &&
                   readFile(path("Open Games.pgn")).find("d6") != std::string::npos;
        },
        10000));
    const auto saved = pgn::parseGame(readFile(path("Open Games.pgn")));
    ASSERT_TRUE(saved.ok());
    kiebitz::repertoire::Tree check(core::Color::White);
    check.add(*saved, 0, 0);
    core::Position position;
    std::vector<core::Move> line;
    for (const char* san : {"e4", "e5", "Nf3", "d6", "d4"}) {
        line.push_back(position.parseSan(san).value());
        position.play(line.back());
    }
    EXPECT_FALSE(check.follow(core::Position(), line).left); // the whole game line is in it

    // A different move of ours where the repertoire has one: only added if confirmed.
    action(window, QStringLiteral("Go to Start"))->trigger();
    play("c4");
    // One file is named: the one that followed the game the longest, kept from before.
    EXPECT_EQ(panel->statusText(core::Color::White),
              QStringLiteral("Left the book with 1. c4 · Open Games plays e4 or d4"));
    const std::string before = readFile(path("Open Games.pgn"));
    answerNextMessage(false);
    emit panel->addLineRequested(core::Color::White);
    EXPECT_EQ(readFile(path("Open Games.pgn")), before);
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        panel->grab().save(QDir(dir).filePath(QStringLiteral("repertoire-check.png")));
    }
    // The repertoire's moves are chips: clicking one plays it where the game left the book.
    QPushButton* e4 = nullptr;
    for (auto* button : panel->findChildren<QPushButton*>()) {
        if (button->property("role").toString() == u"moveChip" && button->text() == u"e4") {
            e4 = button;
        }
    }
    ASSERT_NE(e4, nullptr);
    QTest::mouseClick(e4, Qt::LeftButton);
    EXPECT_TRUE(panel->statusText(core::Color::White).startsWith(u"In book · Open Games"))
        << panel->statusText(core::Color::White).toStdString();

    // Only the side the board is viewed from is checked.
    action(window, QStringLiteral("Flip Board"))->trigger();
    EXPECT_EQ(panel->statusText(core::Color::White), QString());
    action(window, QStringLiteral("Flip Board"))->trigger();
    EXPECT_FALSE(panel->statusText(core::Color::White).isEmpty());

    // Jump to Line: the repertoire file opens at the game's position.
    action(window, QStringLiteral("Go to Start"))->trigger();
    play("e4");
    play("e5");
    const std::string here = fen();
    QSignalSpy opened(&window, &MainWindow::fileOpened);
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("jumpWhite")), Qt::LeftButton);
    ASSERT_TRUE(opened.wait(5000));
    QApplication::processEvents();
    EXPECT_EQ(tabs->tabText(tabs->currentIndex()), QStringLiteral("Open Games"));
    EXPECT_EQ(fen(), here);
}

TEST_F(DocumentsTest, PracticingARepertoireFile) {
    writeFile(path("rep.pgn"), "[FileType \"Repertoire\"]\n[Orientation \"white\"]\n\n"
                               "1. e4 { Best by test [%clk 0:05:00] } e5 2. Nf3 *\n");
    scanLibrary();
    open(path("rep.pgn"));
    auto* panel = window.findChild<kiebitz::app::RepertoirePanel*>();
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->practiceText(), QStringLiteral("2 positions to practice"));
    auto* status = window.findChild<QLabel*>(QStringLiteral("practiceStatus"));
    ASSERT_NE(status, nullptr);

    emit panel->practiceRequested(false, 15);
    EXPECT_FALSE(notation->isVisible()); // the moves would give the answers away
    EXPECT_TRUE(status->isVisible());
    EXPECT_EQ(fen(), core::Position().fen());

    play("d4"); // wrong: taken back, the right move shown with the file's explanation
    EXPECT_TRUE(status->text().contains(QStringLiteral("e4")));
    EXPECT_EQ(fen(), core::Position().fen());
    EXPECT_EQ(status->text(), QStringLiteral("Not quite. The repertoire plays 1. e4"));
    const auto* why = window.findChild<QLabel*>(QStringLiteral("practiceWhy"));
    ASSERT_NE(why, nullptr);
    EXPECT_EQ(why->text(), QStringLiteral("Best by test"));
    EXPECT_TRUE(why->isVisible());
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        window.resize(1200, 760);
        QTest::qWait(100);
        window.grab().save(QDir(dir).filePath(QStringLiteral("practice-mistake.png")));
    }
    const auto* summary = window.findChild<QLabel*>(QStringLiteral("practiceSummary"));
    play("e4"); // right: the opponent answers with the repertoire's reply
    ASSERT_TRUE(QTest::qWaitFor([&] { return fen().find(" w ") != std::string::npos; }, 3000));
    play("Nf3");
    ASSERT_TRUE(QTest::qWaitFor([&] { return status->text().startsWith(u"Done"); }, 3000));
    const auto* progress = window.findChild<QLabel*>(QStringLiteral("practiceProgress"));
    EXPECT_TRUE(progress->text().contains(QStringLiteral("1 mistake")));
    EXPECT_TRUE(summary->text().contains(QStringLiteral("To look at again: 1. e4")));
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        window.resize(1200, 760);
        QTest::qWait(100);
        window.grab().save(QDir(dir).filePath(QStringLiteral("practice-done.png")));
    }
    // Stop: the file again, untouched.
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("practiceStop")),
                      Qt::LeftButton);
    EXPECT_TRUE(notation->isVisible());
    EXPECT_EQ(readFile(path("rep.pgn")).find("d4"), std::string::npos);
}

TEST_F(DocumentsTest, PracticeStartsWhereTheFileDoesOrTakesTheWholeRepertoire) {
    // Black: a Caro-Kann file (its lines part after 1. e4 c6) and a Nimzo file.
    writeFile(path("caro.pgn"), "[FileType \"Repertoire\"]\n[Orientation \"black\"]\n\n"
                                "1. e4 c6 2. d4 (2. Nc3 d5) 2... d5 *\n");
    writeFile(path("nimzo.pgn"), "[FileType \"Repertoire\"]\n[Orientation \"black\"]\n\n"
                                 "1. d4 Nf6 2. c4 e6 3. Nc3 Bb4 *\n");
    scanLibrary();
    open(path("caro.pgn"));
    auto* panel = window.findChild<kiebitz::app::RepertoirePanel*>();
    ASSERT_NE(panel, nullptr);
    // Counted from where it starts: 2... d5 after 2. d4 and after 2. Nc3.
    EXPECT_EQ(panel->practiceText(), QStringLiteral("2 positions to practice"));
    const auto* status = window.findChild<QLabel*>(QStringLiteral("practiceStatus"));
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        window.resize(1200, 760);
        panel->show();
        QTest::qWait(100);
        panel->grab().save(QDir(dir).filePath(QStringLiteral("repertoire-practice.png")));
    }

    // The file opens where it starts, a branch point: its two continuations show as arrows.
    EXPECT_EQ(board->candidates().size(), 2U);

    // Practice begins after 1. e4 c6: the opponent's 2nd move comes first, then yours. The
    // continuations' arrows go: they'd give the answers away and belong to another position.
    emit panel->practiceRequested(false, 0);
    EXPECT_TRUE(board->candidates().empty());
    EXPECT_TRUE(board->engineMoves().empty());
    ASSERT_TRUE(QTest::qWaitFor([&] { return fen().find(" b ") != std::string::npos; }, 3000));
    EXPECT_TRUE(fen().find("2p5") != std::string::npos) << fen(); // 1... c6 already there
    EXPECT_EQ(board->position().fullmoveNumber(), 2);
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("practiceStop")),
                      Qt::LeftButton);

    // The whole Black repertoire: both files together, in a tab of its own, from the start.
    emit panel->wholeRepertoireRequested(0);
    ASSERT_TRUE(QTest::qWaitFor(
        [&] {
            const auto* d = window.document();
            return d != nullptr && d->wholeRepertoire == core::Color::Black;
        },
        5000));
    const auto* whole = window.document();
    EXPECT_EQ(whole->title(), QStringLiteral("Black Repertoire"));
    EXPECT_EQ(whole->game.children(core::Game::root()).size(), 2U); // 1. e4 and 1. d4
    EXPECT_TRUE(status->isVisible());
    // The opponent's first move (1. e4 or 1. d4), then it's yours.
    ASSERT_TRUE(QTest::qWaitFor([&] { return fen().find(" b ") != std::string::npos; }, 3000));
    EXPECT_EQ(board->position().fullmoveNumber(), 1);
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("practiceStop")),
                      Qt::LeftButton);
    // Nothing was written anywhere.
    EXPECT_EQ(readFile(path("caro.pgn")).find("Nf6"), std::string::npos);
}

TEST_F(DocumentsTest, MistakesAreRememberedAndPractisedAgain) {
    // Black, so earlier tests' White mistakes (same statistics file) don't count here. It
    // starts at the very beginning (else after 1. d4 Nf6, where its lines part).
    writeFile(path("kid.pgn"), "[FileType \"Repertoire\"]\n[Orientation \"black\"]\n"
                               "[StartsAt \"\"]\n\n"
                               "1. d4 Nf6 2. c4 (2. Nf3 g6) g6 *\n");
    scanLibrary();
    open(path("kid.pgn"));
    auto* panel = window.findChild<kiebitz::app::RepertoirePanel*>();
    auto* mistakes = window.findChild<QPushButton*>(QStringLiteral("practiceMistakes"));
    ASSERT_NE(mistakes, nullptr);
    EXPECT_FALSE(mistakes->isVisibleTo(panel)); // none yet
    const auto* status = window.findChild<QLabel*>(QStringLiteral("practiceStatus"));
    const auto* title = window.findChild<QLabel*>(QStringLiteral("practiceStatus"))
                            ->parentWidget()
                            ->findChildren<QLabel*>()
                            .front();
    const auto answer = [&](std::string_view wrong, std::string_view right) {
        ASSERT_TRUE(QTest::qWaitFor([&] { return status->text() == u"Your move"; }, 3000));
        if (!wrong.empty()) {
            play(wrong);
        }
        play(right);
    };
    // Every line once: 1... Nf6 is asked twice (both lines), 2... g6 once each.
    emit panel->practiceRequested(false, 15);
    for (int line = 0; line < 2; ++line) {
        answer(line == 0 ? "d5" : "", "Nf6"); // one mistake, the first time only
        answer("", "g6");
    }
    ASSERT_TRUE(QTest::qWaitFor([&] { return status->text().startsWith(u"Done"); }, 3000));
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("practiceStop")),
                      Qt::LeftButton);
    // Remembered: the file offers its mistakes (the position after 1. d4).
    ASSERT_TRUE(QTest::qWaitFor([&] { return mistakes->isVisibleTo(panel); }, 3000));
    EXPECT_EQ(mistakes->text(), QStringLiteral("Practice Mistakes (1)"));
    QTest::mouseClick(mistakes, Qt::LeftButton);
    EXPECT_TRUE(title->text().startsWith(u"PRACTICE MISTAKES"));
    answer("", "Nf6"); // through the mistake: both lines, then done
    answer("", "g6");
    answer("", "Nf6");
    answer("", "g6");
    ASSERT_TRUE(QTest::qWaitFor([&] { return status->text().startsWith(u"Done"); }, 3000));
}

TEST_F(DocumentsTest, PracticeStartsFromTheLibrary) {
    writeFile(path("rep.pgn"), "[FileType \"Repertoire\"]\n[Orientation \"white\"]\n\n"
                               "1. e4 e5 2. Nf3 *\n");
    scanLibrary();
    window.showSection(MainWindow::Section::Library);
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("libraryFiles"));
    tree->setCurrentItem(tree->findItems(QStringLiteral("rep"), Qt::MatchExactly).value(0));
    auto* practice = window.findChild<QPushButton*>(QStringLiteral("libraryPractice"));
    ASSERT_NE(practice, nullptr);
    QTest::mouseClick(practice, Qt::LeftButton);
    const auto* status = window.findChild<QLabel*>(QStringLiteral("practiceStatus"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return status->isVisible(); }, 5000));
    EXPECT_EQ(fen(), core::Position().fen()); // from the start, your move
}

TEST(NewFile, TypeAndSideGoIntoTheHeaders) {
    using kiebitz::app::NewFileDialog;
    const std::string empty =
        NewFileDialog::fileText(QStringLiteral("Najdorf"), kiebitz::library::FileKind::Repertoire,
                                core::Color::Black, QString());
    const auto games = pgn::indexGames(empty);
    ASSERT_EQ(games.size(), 1U);
    EXPECT_EQ(games[0].header("Event"), "Najdorf");
    EXPECT_EQ(games[0].header("FileType"), "Repertoire");
    EXPECT_EQ(games[0].header("Orientation"), "black");
    // Given PGN keeps its moves and gets the type.
    const std::string given =
        NewFileDialog::fileText(QStringLiteral("x"), kiebitz::library::FileKind::Puzzle,
                                core::Color::White, QStringLiteral("[White \"A\"]\n\n1. e4 e5 *"));
    const auto parsed = pgn::parseGame(given);
    ASSERT_TRUE(parsed.ok());
    EXPECT_EQ(parsed->header("FileType"), "Puzzle");
    EXPECT_EQ(parsed->header("Orientation"), "");
    EXPECT_EQ(parsed->mainLine().size(), 3U);
}

TEST_F(DocumentsTest, MoveListInRows) {
    writeFile(path("rows.pgn"),
              "[Result \"*\"]\n\n1. d4 Nf6 { Flexible } 2. c4 (2. Bg5 Ne4) e6 *\n");
    open(path("rows.pgn"));
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("notationRows")),
                      Qt::LeftButton);
    EXPECT_TRUE(notation->rows());
    const QString text = notation->plainText().replace(QChar(0xA0), QLatin1Char(' '));
    // Rows, the comment as its own paragraph, the variation as a block, then a new row for e6.
    EXPECT_EQ(text, QStringLiteral("1 d4 Nf6\nFlexible\n2 c4\n2. Bg5 Ne4\n2 … e6"));
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        window.resize(1200, 760);
        QTest::qWait(100);
        window.grab().save(QDir(dir).filePath(QStringLiteral("notation-rows.png")));
    }
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("notationText")),
                      Qt::LeftButton);
    EXPECT_FALSE(notation->rows());
}

TEST_F(DocumentsTest, ChangesToAFileSaveThemselves) {
    writeFile(path("game.pgn"), kOneGame);
    open(path("game.pgn"));
    action(window, QStringLiteral("Go to End"))->trigger();
    QSignalSpy saved(&window, &MainWindow::fileSaved);
    play("Nc6"); // no ⌘S: saved a moment after the last change
    ASSERT_TRUE(saved.wait(5000));
    const auto game = pgn::parseGame(readFile(path("game.pgn")));
    ASSERT_TRUE(game.ok());
    EXPECT_EQ(game->mainLine().size(), 5U); // root + e4 e5 Nf3 Nc6
    EXPECT_FALSE(tabs->tabText(tabs->currentIndex()).startsWith(u"• "));
}

TEST_F(DocumentsTest, ClosingATabSavesItsChangesWithoutAsking) {
    writeFile(path("game.pgn"), kOneGame);
    open(path("game.pgn"));
    action(window, QStringLiteral("Go to End"))->trigger();
    play("Nc6");
    action(window, QStringLiteral("Close Tab"))->trigger(); // would block if it asked
    const auto game = pgn::parseGame(readFile(path("game.pgn")));
    ASSERT_TRUE(game.ok());
    EXPECT_EQ(game->mainLine().size(), 5U);
}

TEST_F(DocumentsTest, MoveListShowsClocksAndWhoWroteAComment) {
    // As exported from a Lichess study: clocks after every move, a friend's note with [%anno].
    QFile file(path("study.pgn"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("[Event \"Study\"]\n[Result \"*\"]\n\n1. e4 { [%clk 1:24:00] } e5 { [%eval 0.3] "
               "[%anno \"jonathanlund123\", jonathanlund123] trengte du å bruke en halvtime på "
               "a6? [%clk 0:47:00] } 2. Nf3 { [%clk 1:23:30] } *\n");
    file.close();
    open(path("study.pgn"));
    notation->resize(420, 200);
    const QString text = notation->plainText().replace(QChar(0xA0), QLatin1Char(' '));
    EXPECT_EQ(text, QStringLiteral("1. e4 1:24:00 e5 47:00 jonathanlund123: trengte du å bruke en "
                                   "halvtime på a6? 2. Nf3 1:23:30"));
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        QImage image(notation->size() * 2, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(2);
        image.fill(style.colors().surface); // the list is see-through; this is what's behind it
        notation->render(&image);
        image.save(QDir(dir).filePath(QStringLiteral("comments.png")));
    }
}

TEST_F(DocumentsTest, TwoTabsOfOneLibraryFileKeepEachOthersChanges) {
    writeFile(path("three.pgn"), kThreeGames);
    scanLibrary();
    // Both games edited before either is saved: the second save must not undo the first.
    const auto file = window.library().files()[0].pgn;
    std::vector<int> tabOf;
    for (const int game : {0, 2}) {
        QSignalSpy opened(&window, &MainWindow::fileOpened);
        window.openGame(file, game);
        ASSERT_TRUE(opened.wait(5000));
        tabOf.push_back(tabs->currentIndex());
        action(window, QStringLiteral("Go to End"))->trigger();
        play(game == 0 ? "Nf3" : "c4");
    }
    saveAndWait(); // game 3
    tabs->setCurrentIndex(tabOf[0]);
    saveAndWait(); // game 1
    const auto games = pgn::parseAll(readFile(path("three.pgn")));
    ASSERT_TRUE(games.ok()) << games.error().message;
    ASSERT_EQ(games->size(), 3U);
    EXPECT_EQ((*games)[0].mainLine().size(), 4U); // 1. d4 d5 2. Nf3
    EXPECT_EQ((*games)[1].mainLine().size(), 4U); // untouched
    EXPECT_EQ((*games)[2].mainLine().size(), 4U); // 1. Nf3 d5 2. c4
}

/// Scans `folder` with a fresh Library using `cache`, and returns how many games it lists.
int scanWithCache(const QString& folder, const QString& cache) {
    Library library;
    library.setCacheFile(cache);
    QSignalSpy scanned(&library, &Library::scanned);
    library.setFolder(folder);
    EXPECT_TRUE(scanned.wait(5000));
    return scanned.isEmpty() ? -1 : scanned.first().at(1).toInt();
}

TEST(LibraryCache, ListsUnchangedFilesWithoutReadingThem) {
    const QTemporaryDir folder;
    const QTemporaryDir cacheDir;
    const QString cache = cacheDir.filePath(QStringLiteral("library.sqlite"));
    const QString file = folder.filePath(QStringLiteral("three.pgn"));
    writeFile(file, kThreeGames);
    ASSERT_EQ(scanWithCache(folder.path(), cache), 3);

    // Unreadable now, but unchanged (same size and date): its games still come from the cache.
    ASSERT_TRUE(QFile::setPermissions(file, QFileDevice::Permissions{}));
    EXPECT_EQ(scanWithCache(folder.path(), cache), 3);
    // Without the cache the file can't be read, so it has no games.
    EXPECT_EQ(scanWithCache(folder.path(), QString()), 0);
    QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

TEST(LibraryCache, ReadsChangedFilesAgain) {
    const QTemporaryDir folder;
    const QTemporaryDir cacheDir;
    const QString cache = cacheDir.filePath(QStringLiteral("library.sqlite"));
    const QString file = folder.filePath(QStringLiteral("games.pgn"));
    writeFile(file, kThreeGames);
    ASSERT_EQ(scanWithCache(folder.path(), cache), 3);
    writeFile(file, kOneGame);
    EXPECT_EQ(scanWithCache(folder.path(), cache), 1);
}

TEST_F(DocumentsTest, LibraryListsFilesBeforeReadingThem) {
    // The file list comes first (instant even for files iCloud has to download), the games as
    // each file is read; the Library never says "empty" in between.
    writeFile(path("one.pgn"), kOneGame);
    writeFile(path("three.pgn"), kThreeGames);
    std::size_t listed = 0;
    bool sawUnread = false;
    QObject::connect(&window.library(), &Library::progress, &window, [&] {
        if (listed == 0) {
            listed = window.library().files().size();
            for (const auto& file : window.library().files()) {
                sawUnread = sawUnread || !file.pgn;
            }
        }
    });
    QSignalSpy scanned(&window.library(), &Library::scanned);
    window.library().setFolder(folder.path());
    ASSERT_TRUE(scanned.wait(5000));
    EXPECT_EQ(listed, 2U);  // both files known before any was read
    EXPECT_TRUE(sawUnread); // ... and before their games were
    EXPECT_FALSE(window.library().isReading());
    for (const auto& file : window.library().files()) {
        ASSERT_TRUE(file.pgn);
    }
}

TEST_F(DocumentsTest, LibraryNoticesNewFiles) {
    writeFile(path("one.pgn"), kOneGame);
    scanLibrary();
    ASSERT_EQ(window.library().files().size(), 1U);
    QSignalSpy scanned(&window.library(), &Library::scanned);
    writeFile(path("added.pgn"), kThreeGames);
    ASSERT_TRUE(scanned.wait(5000)); // the folder watcher triggers a rescan
    EXPECT_EQ(window.library().files().size(), 2U);
}

TEST_F(DocumentsTest, SaveAsWritesANewFile) {
    window.showSection(MainWindow::Section::Board);
    play("e4");
    QSignalSpy saved(&window, &MainWindow::fileSaved);
    window.saveCurrentAs(path("new.pgn"));
    ASSERT_TRUE(saved.wait(5000));
    const auto game = pgn::parseGame(readFile(path("new.pgn")));
    ASSERT_TRUE(game.ok());
    EXPECT_EQ(game->mainLine().size(), 2U);
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("new")); // named after the file now
}

TEST_F(DocumentsTest, NotationEditing) {
    window.showSection(MainWindow::Section::Board);
    play("e4");
    play("e5");
    action(window, QStringLiteral("Back"))->trigger();
    play("c5");
    play("Nf3"); // 1. e4 e5 (1... c5 2. Nf3)
    // Promote 1... c5 to the main line.
    const auto c5Fen = std::string("rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");
    action(window, QStringLiteral("Back"))->trigger(); // on 1... c5
    EXPECT_EQ(fen(), c5Fen);
    action(window, QStringLiteral("Promote Variation"))->trigger();
    action(window, QStringLiteral("Go to Start"))->trigger();
    action(window, QStringLiteral("Forward"))->trigger();
    action(window, QStringLiteral("Forward"))->trigger();
    EXPECT_EQ(fen(), c5Fen); // the main line now goes 1... c5

    // Delete from here removes c5 and what follows; we land on its parent.
    action(window, QStringLiteral("Delete From Here"))->trigger();
    EXPECT_EQ(fen(), "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    action(window, QStringLiteral("Forward"))->trigger();
    EXPECT_EQ(fen(), "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");
}

TEST_F(DocumentsTest, NagsAreExclusivePerGroup) {
    window.showSection(MainWindow::Section::Board);
    play("e4");
    const auto id = core::NodeId{1};
    emit notation->nagToggled(id, core::nag::kGood);
    emit notation->nagToggled(id, core::nag::kInteresting); // replaces "!"
    emit notation->nagToggled(id, core::nag::kWhiteBetter); // a position mark: kept alongside
    QSignalSpy saved(&window, &MainWindow::fileSaved);
    window.saveCurrentAs(path("nags.pgn"));
    ASSERT_TRUE(saved.wait(5000));
    EXPECT_NE(readFile(path("nags.pgn")).find("1. e4 $5 $16"), std::string::npos);
}

TEST_F(DocumentsTest, CommentBoxEditsTheCurrentMoveAndKeepsItsArrowKeys) {
    window.showSection(MainWindow::Section::Board);
    play("e4");
    auto* box = window.findChild<QPlainTextEdit*>(QStringLiteral("commentBox"));
    ASSERT_NE(box, nullptr);
    box->setFocus();
    QTest::keyClicks(box, QStringLiteral("Best by test"));
    QTest::keyClick(box, Qt::Key_Left); // moves the text cursor, not the game
    EXPECT_EQ(fen(), "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    QSignalSpy saved(&window, &MainWindow::fileSaved);
    window.saveCurrentAs(path("comment.pgn"));
    ASSERT_TRUE(saved.wait(5000));
    EXPECT_NE(readFile(path("comment.pgn")).find("1. e4 {Best by test}"), std::string::npos);
}

TEST_F(DocumentsTest, MenuShortcutsLeaveTheCommentBoxAlone) {
    // macOS's menu bar takes ⌘⌫ before the comment box sees it; it must still edit the text
    // there rather than delete the move.
    window.showSection(MainWindow::Section::Board);
    play("e4");
    auto* box = window.findChild<QPlainTextEdit*>(QStringLiteral("commentBox"));
    box->setFocus();
    QTest::keyClicks(box, QStringLiteral("solid"));
    action(window, QStringLiteral("Delete From Here"))->trigger(); // what the menu would do
    action(window, QStringLiteral("Promote Variation"))->trigger();
    EXPECT_EQ(fen(),
              "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"); // e4 still there
    EXPECT_TRUE(box->toPlainText().isEmpty()); // ⌘⌫ deleted the line's text instead

    board->setFocus(); // with the board focused, the same command deletes the move
    action(window, QStringLiteral("Delete From Here"))->trigger();
    EXPECT_EQ(fen(), std::string(kiebitz::core::kStartFen));
}

TEST_F(DocumentsTest, HeadersFromTheInfoPanel) {
    window.showSection(MainWindow::Section::Board);
    auto* info = window.findChild<kiebitz::app::GameInfoPanel*>();
    ASSERT_NE(info, nullptr);
    emit info->headerEdited(QStringLiteral("White"), QStringLiteral("Carlsen"));
    emit info->headerEdited(QStringLiteral("Black"), QStringLiteral("Nepomniachtchi"));
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("• Carlsen – Nepomniachtchi"));
}

TEST_F(DocumentsTest, CopyAndPaste) {
    window.showSection(MainWindow::Section::Board);
    play("d4");
    action(window, QStringLiteral("Copy Game as PGN"))->trigger();
    EXPECT_TRUE(QApplication::clipboard()->text().contains(QStringLiteral("1. d4 *")));
    action(window, QStringLiteral("Copy Position as FEN"))->trigger();
    EXPECT_EQ(QApplication::clipboard()->text().toStdString(), fen());

    // Pasting a FEN opens that position in a new tab.
    QApplication::clipboard()->setText(QStringLiteral("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"));
    board->setFocus();
    const int count = tabs->count();
    action(window, QStringLiteral("Paste"))->trigger();
    EXPECT_EQ(tabs->count(), count + 1);
    EXPECT_EQ(fen(), "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
}

/// Design review: the Library and a game with variations, saved as PNGs when
/// KIEBITZ_SNAPSHOT_DIR is set. KIEBITZ_REVIEW_LIBRARY may name a real folder to show (it is
/// only read); otherwise the test's own files are used.
TEST_F(DocumentsTest, Snapshots) {
    const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR");
    const QString review = qEnvironmentVariable("KIEBITZ_REVIEW_LIBRARY");
    const auto save = [&](const QString& name) {
        QApplication::processEvents();
        const QImage image = window.grab().toImage();
        EXPECT_FALSE(image.isNull());
        if (!dir.isEmpty()) {
            image.save(QDir(dir).filePath(name + QStringLiteral(".png")));
        }
    };
    window.resize(1280, 800);
    {
        // An empty library first.
        QSignalSpy emptyScan(&window.library(), &Library::scanned);
        QDir(folder.path()).mkdir(QStringLiteral("empty"));
        window.library().setFolder(folder.filePath(QStringLiteral("empty")));
        ASSERT_TRUE(emptyScan.wait(10000));
        window.showSection(MainWindow::Section::Library);
        save(QStringLiteral("library-empty"));
    }
    writeFile(path("three.pgn"), kThreeGames);
    writeFile(path("one.pgn"), kOneGame);
    QSignalSpy scanned(&window.library(), &Library::scanned);
    window.library().setFolder(review.isEmpty() ? folder.path() : review);
    ASSERT_TRUE(scanned.wait(10000));
    window.showSection(MainWindow::Section::Library);
    QTest::qWait(300); // let the preview's background parse finish
    save(QStringLiteral("library"));

    // The biggest game in the library, opened at a move with variations.
    std::shared_ptr<kiebitz::app::PgnFile> biggest;
    int index = 0;
    std::size_t length = 0;
    for (const auto& file : window.library().files()) {
        for (std::size_t i = 0; i < file.pgn->games.size(); ++i) {
            if (file.pgn->games[i].length > length) {
                length = file.pgn->games[i].length;
                biggest = file.pgn;
                index = static_cast<int>(i);
            }
        }
    }
    ASSERT_TRUE(biggest);
    QSignalSpy opened(&window, &MainWindow::fileOpened);
    window.openGame(biggest, index);
    ASSERT_TRUE(opened.wait(10000));
    for (int i = 0; i < 6; ++i) {
        action(window, QStringLiteral("Forward"))->trigger();
    }
    QApplication::processEvents();
    save(QStringLiteral("game"));
}

} // namespace

TEST_F(DocumentsTest, TheGapCheckListsUnansweredMovesAndOpensOne) {
    writeFile(path("sicilian.pgn"), "[FileType \"Repertoire\"]\n[Orientation \"black\"]\n\n"
                                    "1. e4 c5 2. Nf3 d6 *\n");
    scanLibrary();
    auto* view = window.findChild<kiebitz::app::LibraryView*>();
    ASSERT_NE(view, nullptr);
    // A made-up source: 1. e4 or 1. d4 from the start; after 1. e4 c5, 2. Nf3 or 2. c3.
    view->setGapSource([] {
        kiebitz::app::GapSource source;
        source.name = QStringLiteral("Test games");
        source.explore = [](std::span<const core::Move> line) {
            core::Position position;
            std::string key;
            for (const core::Move move : line) {
                key += position.san(move) + " ";
                position.play(move);
            }
            std::vector<kiebitz::repertoire::MoveCount> moves;
            const auto add = [&](const char* san, std::uint64_t games) {
                moves.push_back({position.parseSan(san).value(), games});
            };
            if (key.empty()) {
                add("e4", 600);
                add("d4", 400);
            } else if (key == "e4 c5 ") {
                add("Nf3", 450);
                add("c3", 150);
            }
            return moves;
        };
        return source;
    });
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("libraryFiles"));
    tree->setCurrentItem(
        tree->findItems(QStringLiteral("sicilian"), Qt::MatchExactly | Qt::MatchRecursive)
            .value(0));
    ASSERT_TRUE(QTest::qWaitFor([&] { return view->repertoireStats().value(0) != u"…"; }, 5000));
    QSignalSpy checked(view, &kiebitz::app::LibraryView::gapsChecked);
    view->checkGaps();
    ASSERT_TRUE(checked.wait(5000));
    // 1. d4: 40% of games, nothing against it; 2. c3: 25% of the games after 1. e4 c5.
    EXPECT_EQ(view->repertoireGaps(), (QStringList{QStringLiteral("1. d4  40%  40% of games"),
                                                   QStringLiteral("2. c3  25%  15% of games")}));
    EXPECT_TRUE(view->gapsStatus().startsWith(u"2 gaps against Test games"));
    // One line, never parting: it starts at the start.
    EXPECT_EQ(window.findChild<QLabel*>(QStringLiteral("repertoireStartLine"))->text(),
              QStringLiteral("Starts at the beginning"));
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        window.resize(1200, 760);
        QTest::qWait(100);
        window.grab().save(QDir(dir).filePath(QStringLiteral("library-gaps.png")));
    }
    // Clicked: the file opens after 1. e4 c5 with 2. c3 played, for your answer.
    auto* gaps = window.findChild<QTreeWidget*>(QStringLiteral("repertoireGaps"));
    QSignalSpy opened(&window, &MainWindow::fileOpened);
    emit gaps->itemClicked(gaps->topLevelItem(1), 0);
    ASSERT_TRUE(opened.wait(5000));
    QApplication::processEvents();
    EXPECT_EQ(fen(), "rnbqkbnr/pp1ppppp/8/2p5/4P3/2P5/PP1P1PPP/RNBQKBNR b KQkq - 0 2");
}

TEST_F(DocumentsTest, TheGapCheckStartsWhereTheFileDoes) {
    // A Caro-Kann file: every line begins 1. e4 c6, then 2. d4 or 2. Nc3.
    writeFile(path("caro.pgn"), "[FileType \"Repertoire\"]\n[Orientation \"black\"]\n\n"
                                "1. e4 c6 2. d4 (2. Nc3 d5) 2... d5 *\n");
    scanLibrary();
    auto* view = window.findChild<kiebitz::app::LibraryView*>();
    ASSERT_NE(view, nullptr);
    // From the start 1. d4 would be a gap; after 1. e4 c6, 2. c4 is.
    view->setGapSource([] {
        kiebitz::app::GapSource source;
        source.name = QStringLiteral("Test games");
        source.explore = [](std::span<const core::Move> line) {
            core::Position position;
            for (const core::Move move : line) {
                position.play(move);
            }
            std::vector<kiebitz::repertoire::MoveCount> moves;
            const auto add = [&](const char* san, std::uint64_t games) {
                moves.push_back({position.parseSan(san).value(), games});
            };
            if (line.empty()) {
                add("e4", 600);
                add("d4", 400);
            } else if (line.size() == 2) {
                add("d4", 500);
                add("Nc3", 300);
                add("c4", 200);
            }
            return moves;
        };
        return source;
    });
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("libraryFiles"));
    tree->setCurrentItem(
        tree->findItems(QStringLiteral("caro"), Qt::MatchExactly | Qt::MatchRecursive).value(0));
    // Beside the openings: where it starts, where its lines part.
    auto* startLine = window.findChild<QLabel*>(QStringLiteral("repertoireStartLine"));
    auto* startBoard =
        window.findChild<kiebitz::app::BoardView*>(QStringLiteral("repertoireStartBoard"));
    ASSERT_NE(startLine, nullptr);
    ASSERT_NE(startBoard, nullptr);
    ASSERT_TRUE(
        QTest::qWaitFor([&] { return startLine->text() == u"Starts after 1. e4 c6"; }, 5000));
    const std::string afterC6 = "rnbqkbnr/pp1ppppp/2p5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2";
    EXPECT_EQ(startBoard->position().fen(), afterC6);
    EXPECT_TRUE(startBoard->isFlipped()); // Black's file, Black's side
    const auto* move = window.findChild<QLabel*>(QStringLiteral("repertoireMove"));
    EXPECT_EQ(move->text(), QStringLiteral("After 1… c6  ·  where the file starts"));
    const auto* name = window.findChild<QLabel*>(QStringLiteral("repertoireName"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return name->text() == u"Caro-Kann Defense"; }, 20000));
    // Stepped through with the arrow keys, along its first moves.
    QTest::mouseClick(startBoard, Qt::LeftButton);
    QTest::keyClick(startBoard, Qt::Key_Right);
    EXPECT_EQ(move->text(), QStringLiteral("After 2. d4"));
    QTest::keyClick(startBoard, Qt::Key_Home);
    EXPECT_EQ(move->text(), QStringLiteral("Start position"));
    EXPECT_EQ(name->text(), QStringLiteral("caro"));

    QSignalSpy checked(view, &kiebitz::app::LibraryView::gapsChecked);
    view->checkGaps();
    ASSERT_TRUE(checked.wait(5000));
    EXPECT_EQ(view->repertoireGaps(), QStringList{QStringLiteral("2. c4  20%  20% of games")});
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        window.resize(1200, 760);
        QTest::qWait(100);
        window.grab().save(QDir(dir).filePath(QStringLiteral("library-start.png")));
    }

    // Opened, the file shows where it starts, and the move list flags it.
    QSignalSpy opened(&window, &MainWindow::fileOpened);
    window.openFile(path("caro.pgn"));
    ASSERT_TRUE(opened.wait(5000));
    QApplication::processEvents();
    EXPECT_EQ(fen(), afterC6);
    auto* notation = window.findChild<kiebitz::app::NotationView*>();
    ASSERT_NE(notation, nullptr);
    const core::NodeId c6 = notation->start();
    ASSERT_NE(c6, core::kNoNode);
    EXPECT_EQ(window.document()->game.node(c6).san, "c6");
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        window.grab().save(QDir(dir).filePath(QStringLiteral("notation-start.png")));
    }

    // "Start From the Beginning" (the move list's menu): kept in the file, the flag goes, and
    // 1. d4 is a gap now.
    emit notation->startRequested(core::Game::root());
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return readFile(path("caro.pgn")).find("[StartsAt \"\"]") != std::string::npos; },
        5000));
    ASSERT_TRUE(QTest::qWaitFor([&] { return notation->start() == core::kNoNode; }, 5000));
    window.showSection(MainWindow::Section::Library);
    ASSERT_TRUE(
        QTest::qWaitFor([&] { return startLine->text() == u"Starts at the beginning"; }, 5000));
    view->checkGaps();
    ASSERT_TRUE(checked.wait(5000));
    EXPECT_EQ(view->repertoireGaps().value(0), QStringLiteral("1. d4  40%  40% of games"));

    // "Start Here" on 1... c6, where its lines part anyway: the header goes again.
    window.showSection(MainWindow::Section::Board);
    emit notation->startRequested(
        window.document()->game.mainChild(window.document()->game.mainChild(core::Game::root())));
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return readFile(path("caro.pgn")).find("StartsAt") == std::string::npos; }, 5000));
    ASSERT_TRUE(QTest::qWaitFor([&] { return notation->start() != core::kNoNode; }, 5000));

    // The preview's "Open Here": the file at the position shown (after 2... d5).
    window.showSection(MainWindow::Section::Library);
    ASSERT_TRUE(
        QTest::qWaitFor([&] { return startLine->text() == u"Starts after 1. e4 c6"; }, 5000));
    QTest::mouseClick(startBoard, Qt::LeftButton);
    QTest::keyClick(startBoard, Qt::Key_End);
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("repertoireOpenHere")),
                      Qt::LeftButton);
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return fen() == "rnbqkbnr/pp2pppp/2p5/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 3"; },
        5000));
}

TEST_F(DocumentsTest, LibraryGamesSortByAnyColumnAndFilterByDate) {
    writeFile(path("club.pgn"), "[White \"Carol\"]\n[Black \"x\"]\n[Date \"2021.03.01\"]\n"
                                "[Result \"1-0\"]\n\n1. e4 e5 1-0\n\n"
                                "[White \"alice\"]\n[Black \"x\"]\n[Date \"2023.07.15\"]\n"
                                "[Result \"0-1\"]\n\n1. d4 d5 0-1\n\n"
                                "[White \"Bob\"]\n[Black \"x\"]\n[Date \"2023.01.02\"]\n"
                                "[Result \"1-0\"]\n\n1. c4 c5 1-0\n");
    scanLibrary();
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("libraryFiles"));
    tree->setCurrentItem(
        tree->findItems(QStringLiteral("club"), Qt::MatchExactly | Qt::MatchRecursive).value(0));
    auto* games = window.findChild<QTableView*>(QStringLiteral("libraryGames"));
    ASSERT_NE(games, nullptr);
    const auto whites = [&] {
        QStringList names;
        for (int row = 0; row < games->model()->rowCount(); ++row) {
            names << games->model()->index(row, 1).data().toString(); // # is column 0
        }
        return names;
    };
    ASSERT_TRUE(QTest::qWaitFor([&] { return games->model()->rowCount() == 3; }, 5000));
    EXPECT_EQ(whites(), (QStringList{"Carol", "alice", "Bob"})); // the file's order
    // A click on DATE's title: oldest first; again: newest first.
    const auto* header = games->horizontalHeader();
    const QPoint date(header->sectionViewportPosition(4) + 10, header->height() / 2);
    QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, date);
    EXPECT_EQ(whites(), (QStringList{"Carol", "Bob", "alice"}));
    QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, date);
    EXPECT_EQ(whites(), (QStringList{"alice", "Bob", "Carol"}));
    // A third click: the file's order again.
    QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, date);
    EXPECT_EQ(whites(), (QStringList{"Carol", "alice", "Bob"}));
    // From July 2023 on: alice's game only.
    auto* dates = window.findChild<kiebitz::app::DateRange*>(QStringLiteral("libraryDate"));
    ASSERT_NE(dates, nullptr);
    dates->setRange(QDate(2023, 7, 1), std::nullopt);
    ASSERT_TRUE(QTest::qWaitFor([&] { return whites() == QStringList{"alice"}; }, 5000));
}
