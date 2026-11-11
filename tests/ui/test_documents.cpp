// M3 in the window: files, the library, tabs, notation editing, comments, headers, clipboard.
// Everything happens in a temporary folder; background work is awaited through signals.

#include "MainWindow.hpp"
#include "board/BoardView.hpp"
#include "kiebitz/pgn/pgn.hpp"
#include "library/Library.hpp"
#include "notation/NotationView.hpp"
#include "panels/GameInfoPanel.hpp"
#include "style/AppStyle.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTabBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>
#include <gtest/gtest.h>

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
        board = window.findChild<BoardView*>();
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
    EXPECT_EQ(otb->childCount(), 2);              // Black, White
    EXPECT_EQ(otb->text(1), QStringLiteral("4")); // 1 + 3 games inside
    // OTB holds only folders: its own list is empty; the games are a level further down.
    tree->setCurrentItem(otb);
    EXPECT_EQ(games->model()->rowCount(), 0);
    const auto* count = window.findChild<QLabel*>(QStringLiteral("libraryListCount"));
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->text(), QStringLiteral("0 games  ·  0 files  ·  2 folders"));
    tree->setCurrentItem(item(QStringLiteral("Black")));
    EXPECT_EQ(games->model()->rowCount(), 3); // b.pgn's games
    // The top level holds top.pgn (and the OTB folder, whose games stay inside it).
    writeFile(path("OTB/notes.pgn"), kOneGame);
    QSignalSpy scanned(&window.library(), &Library::scanned);
    ASSERT_TRUE(scanned.wait(5000));
    tree->setCurrentItem(item(QStringLiteral("OTB")));
    EXPECT_EQ(games->model()->rowCount(), 1); // notes.pgn only, not Black/ or White/
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

TEST_F(LibraryTreeTest, EveryGameRowStartsWithItsFileName) {
    QSignalSpy scanned(&window.library(), &Library::scanned);
    // As En Croissant writes its opening files: every header a "?".
    writeFile(path("Bird Opening.pgn"),
              "[Event \"?\"]\n[White \"?\"]\n[Black \"?\"]\n\n1. f4 d5 *\n");
    ASSERT_TRUE(scanned.wait(5000));
    tree->setCurrentItem(item(QStringLiteral("Bird Opening")));
    EXPECT_EQ(games->model()->index(0, 1).data().toString(), QStringLiteral("Bird Opening"));
    EXPECT_EQ(games->model()->index(0, 2).data().toString(), QString()); // no "?" for White
    tree->setCurrentItem(item(QStringLiteral("top"))); // a game with players has it too
    EXPECT_EQ(games->model()->index(0, 1).data().toString(), QStringLiteral("top"));
    EXPECT_EQ(games->model()->index(0, 2).data().toString(), QStringLiteral("Alice"));
    tree->setCurrentItem(item(QStringLiteral("Bird Opening")));
    const auto* players = window.findChild<QLabel*>(QStringLiteral("previewPlayers"));
    ASSERT_NE(players, nullptr);
    EXPECT_EQ(players->text(), QStringLiteral("Bird Opening"));
}

TEST_F(LibraryTreeTest, ALongFileNameDoesNotWidenTheWindow) {
    const int before = window.minimumSizeHint().width();
    const QString name = QStringLiteral("Symmetrical English - 2.g3 Nc6, Early d4 and b-pawn "
                                        "systems - after 5.a3 + after 5.Nf3 - Training version");
    QSignalSpy scanned(&window.library(), &Library::scanned);
    // No players, as in an opening file: the tab is named after the file too.
    writeFile(folder.filePath(name + QStringLiteral(".pgn")),
              "[White \"?\"]\n[Black \"?\"]\n\n1. e4 *\n");
    ASSERT_TRUE(scanned.wait(5000));
    tree->setCurrentItem(item(name));
    QApplication::processEvents();
    // The heading over the game list is cut off with "…" (the tooltip has all of it) ...
    const auto* title = window.findChild<QLabel*>(QStringLiteral("libraryListTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_TRUE(title->text().endsWith(QStringLiteral("…")));
    EXPECT_EQ(title->toolTip(), name);
    EXPECT_EQ(window.minimumSizeHint().width(), before);

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
    EXPECT_TRUE(
        board->parentWidget()->parentWidget()->parentWidget()->property("focused").toBool());
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
