// Layouts must not jump: when something on a page changes (a timer, a toggle, an engine, a
// game with clocks, a branch point), nothing else on the page moves or changes size.
//
// Each test records where every named, visible widget of the window is, makes one change, and
// lists the widgets that moved. Widgets that appear or go are not counted (that is the
// change itself); widgets whose job is to grow (a list's contents) are named as allowed.

#include "MainWindow.hpp"
#include "board/BoardView.hpp"
#include "databases/DatabaseManager.hpp"
#include "engine/EnginePanel.hpp"
#include "engine/EngineRegistry.hpp"
#include "engine/EngineSection.hpp"
#include "explorer/ExplorerPanel.hpp"
#include "explorer/Tablebase.hpp"
#include "fake_server.hpp"
#include "library/Library.hpp"
#include "profiles/ProfileStore.hpp"
#include "profiles/ProfilesView.hpp"
#include "style/AppStyle.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <memory>

namespace {

using kiebitz::app::BoardView;
using kiebitz::app::MainWindow;
using namespace kiebitz;

using Layout = std::map<QString, QRect>;

/// A widget's name with its named parents, so two widgets of the same name stay apart.
QString pathOf(const QWidget* widget, const QWidget* root) {
    QStringList names;
    for (const QWidget* w = widget; w != nullptr && w != root; w = w->parentWidget()) {
        if (!w->objectName().isEmpty()) {
            names.prepend(w->objectName());
        }
    }
    return names.join(u'/');
}

/// Lets layouts settle: posted layout requests and resizes, then once more.
void settle() {
    for (int k = 0; k < 3; ++k) {
        QApplication::processEvents();
        QTest::qWait(30);
    }
}

/// Where every named widget visible in `root` is, in root's coordinates.
Layout layoutOf(const QWidget* root) {
    settle();
    Layout result;
    std::map<QString, int> seen;
    for (const QWidget* w : root->findChildren<QWidget*>()) {
        if (w->objectName().isEmpty() || w->objectName().startsWith(u"qt_") ||
            !w->isVisibleTo(root)) {
            continue;
        }
        QString key = pathOf(w, root);
        if (const int n = seen[key]++; n > 0) {
            key += QStringLiteral("#%1").arg(n);
        }
        result[key] = QRect(w->mapTo(root, QPoint(0, 0)), w->size());
    }
    return result;
}

/// The widgets, visible before and after, that moved or changed size; except `allowed` (a
/// name anywhere in the path).
QStringList moved(const Layout& before, const Layout& after, const QStringList& allowed = {}) {
    QStringList result;
    int compared = 0;
    for (const auto& [key, rect] : before) {
        const auto found = after.find(key);
        compared += found != after.end() ? 1 : 0;
        if (found == after.end() || found->second == rect) {
            continue;
        }
        const QStringList parts = key.split(u'/');
        if (std::any_of(allowed.begin(), allowed.end(),
                        [&](const QString& name) { return parts.contains(name); })) {
            continue;
        }
        result << QStringLiteral("%1: (%2,%3 %4x%5) -> (%6,%7 %8x%9)")
                      .arg(key)
                      .arg(rect.x())
                      .arg(rect.y())
                      .arg(rect.width())
                      .arg(rect.height())
                      .arg(found->second.x())
                      .arg(found->second.y())
                      .arg(found->second.width())
                      .arg(found->second.height());
    }
    if (compared < 10) { // a page that wasn't shown: the check would prove nothing
        result << QStringLiteral("only %1 widgets to compare").arg(compared);
    }
    return result;
}

std::string text(const QStringList& list) {
    return list.join(u'\n').toStdString();
}

class LayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(folder.isValid());
        QSettings settings;
        settings.remove(QStringLiteral("engines"));
        settings.remove(QStringLiteral("engine"));
        window = std::make_unique<MainWindow>(style);
        window->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window.get()));
        window->setWindowState(Qt::WindowNoState);
        window->resize(1280, 800);
        board = window->findChild<BoardView*>(QStringLiteral("gameBoard"));
        ASSERT_NE(board, nullptr);
        window->showSection(MainWindow::Section::Board);
    }
    void open(const char* name, const std::string& pgn) {
        const QString path = folder.filePath(QString::fromUtf8(name));
        std::ofstream(path.toStdString()) << pgn;
        QSignalSpy opened(window.get(), &MainWindow::fileOpened);
        window->openFile(path);
        ASSERT_TRUE(opened.wait(5000));
    }
    [[nodiscard]] Layout now() const { return layoutOf(window.get()); }
    QString write(const char* name, const std::string& pgn) {
        const QString path = folder.filePath(QString::fromUtf8(name));
        std::ofstream(path.toStdString()) << pgn;
        return path;
    }
    /// A small game database in the window's own database folder (removed by TearDown).
    void addDatabase() {
        auto* manager = window->findChild<app::DatabaseManager*>();
        ASSERT_NE(manager, nullptr);
        QSignalSpy finished(manager, &app::DatabaseManager::jobFinished);
        QSignalSpy changed(manager, &app::DatabaseManager::changed);
        manager->importFile(write("club.pgn", kClub), QStringLiteral("Club games"));
        ASSERT_TRUE(finished.wait(20000));
        ASSERT_TRUE(finished.front().at(1).toString().isEmpty());
        changed.wait(5000);
        m_databases = manager;
    }
    void TearDown() override {
        if (m_databases != nullptr) {
            for (const auto& database : m_databases->databases()) {
                m_databases->remove(database.folder);
            }
        }
    }

    static constexpr const char* kClub =
        "[White \"Carlsen, Magnus\"]\n[Black \"Caruana, Fabiano\"]\n[Result \"1-0\"]\n"
        "[WhiteElo \"2830\"]\n[BlackElo \"2800\"]\n\n1. e4 e5 2. Nf3 Nc6 1-0\n\n"
        "[White \"Smith\"]\n[Black \"Jones\"]\n[Result \"0-1\"]\n\n1. d4 d5 0-1\n\n"
        "[White \"Doe\"]\n[Black \"Roe\"]\n[Result \"1/2-1/2\"]\n\n1. e4 c5 1/2-1/2\n";

    QTemporaryDir folder;
    app::AppStyle style{*qApp};
    std::unique_ptr<MainWindow> window;
    BoardView* board = nullptr;
    app::DatabaseManager* m_databases = nullptr;
};

// ---- The board page ----------------------------------------------------------------------------

TEST_F(LayoutTest, BoardPageStaysPutThroughAGame) {
    // A game with clocks, variations and an opening name.
    open("game.pgn", "[White \"A\"]\n[Black \"B\"]\n\n"
                     "1. e4 {[%clk 0:03:00]} c5 {[%clk 0:03:00]} 2. Nf3 {[%clk 0:02:58]} "
                     "(2. Nc3 Nc6) 2... d6 {[%clk 0:02:57]} 3. d4 {[%clk 0:02:55]} cxd4 "
                     "{[%clk 0:02:54]} 4. Nxd4 {[%clk 0:02:50]} Nf6 {[%clk 0:02:49]} 5. Nc3 "
                     "{[%clk 0:02:45]} a6 {[%clk 0:02:40]} *\n");
    // The list of moves and the comment box show what the move is: their contents change, not
    // where they are.
    const Layout start = now();
    // The clocks come with the first move; a branch point (after 1... c5) shows the
    // continuations; deep in, the title names the Najdorf.
    for (int k = 0; k < 2; ++k) {
        window->findChild<QPushButton*>()->setFocus();
        QTest::keyClick(board, Qt::Key_Right);
    }
    // At the branch point the continuations take room from the move list, and only from it.
    const Layout fork = now();
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        window->grab().save(QDir(dir).filePath(QStringLiteral("board-continuations.png")));
    }
    const QStringList list{QStringLiteral("moveList")};
    EXPECT_TRUE(moved(start, fork, list).isEmpty()) << text(moved(start, fork, list));
    for (int k = 0; k < 8; ++k) {
        QTest::keyClick(board, Qt::Key_Right);
    }
    const Layout najdorf = now();
    EXPECT_TRUE(moved(fork, najdorf, list).isEmpty()) << text(moved(fork, najdorf, list));
}

TEST_F(LayoutTest, BoardPageStaysPutWhenTheEngineStartsAndStops) {
    app::EngineEntry entry;
    entry.name = QStringLiteral("FakeFish 1");
    entry.path = QStringLiteral(KIEBITZ_FAKE_ENGINE);
    entry.threads = 1;
    entry.hashMb = 16;
    window->engines().add(entry);
    auto* panel = window->findChild<app::EnginePanel*>(QStringLiteral("enginePanel"));
    const Layout off = now();
    auto* toggle =
        panel->section(entry.name)->findChild<QPushButton*>(QStringLiteral("engineToggle"));
    QTest::mouseClick(toggle, Qt::LeftButton);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !panel->lines().empty(); }, 5000));
    QTest::qWait(200); // a few more reports
    const Layout on = now();
    // The engine's own lines fill in: that is the change.
    EXPECT_TRUE(moved(off, on, {QStringLiteral("enginePanel")}).isEmpty())
        << text(moved(off, on, {QStringLiteral("enginePanel")}));
    QTest::mouseClick(toggle, Qt::LeftButton);
    const Layout offAgain = now();
    EXPECT_TRUE(moved(on, offAgain, {QStringLiteral("enginePanel")}).isEmpty())
        << text(moved(on, offAgain, {QStringLiteral("enginePanel")}));
}

TEST_F(LayoutTest, BoardPageStaysPutWhenPracticeStartsAndStops) {
    open("rep.pgn", "[FileType \"Repertoire\"]\n[Orientation \"black\"]\n\n"
                    "1. e4 c6 2. d4 (2. Nc3 d5) 2... d5 *\n");
    const Layout game = now();
    auto* practice = window->findChild<QWidget*>(QStringLiteral("repertoirePanel"));
    ASSERT_NE(practice, nullptr);
    QMetaObject::invokeMethod(practice, "practiceRequested", Q_ARG(bool, false), Q_ARG(int, 0));
    QTest::qWait(1000);
    const Layout practising = now();
    EXPECT_TRUE(moved(game, practising).isEmpty()) << text(moved(game, practising));
    QTest::mouseClick(window->findChild<QPushButton*>(QStringLiteral("practiceStop")),
                      Qt::LeftButton);
    const Layout stopped = now();
    EXPECT_TRUE(moved(practising, stopped).isEmpty()) << text(moved(practising, stopped));
}

TEST_F(LayoutTest, ThePanelsTabsKeepTheBoardAndMovesWhereTheyAre) {
    auto* panels = window->findChild<QTabWidget*>(QStringLiteral("panels"));
    ASSERT_NE(panels, nullptr);
    const Layout first = now();
    for (int tab = 1; tab < panels->count(); ++tab) {
        panels->setCurrentIndex(tab);
        const Layout shown = now();
        EXPECT_TRUE(moved(first, shown, {QStringLiteral("panels")}).isEmpty())
            << panels->tabText(tab).toStdString() << "\n"
            << text(moved(first, shown, {QStringLiteral("panels")}));
    }
}

TEST_F(LayoutTest, TheExplorerStaysPutAcrossSourcesFiltersAndViews) {
    addDatabase();
    auto* panels = window->findChild<QTabWidget*>(QStringLiteral("panels"));
    panels->setCurrentWidget(window->findChild<QWidget*>(QStringLiteral("explorerPanel")));
    auto* source = window->findChild<QComboBox*>(QStringLiteral("explorerSource"));
    ASSERT_NE(source, nullptr);
    const auto choose = [&](int index) {
        source->setCurrentIndex(index);
        emit source->activated(index);
    };
    choose(source->findData(QStringLiteral("db:"), Qt::MatchStartsWith));
    const Layout database = now();
    // A filter (the tab's dot), the other views, Lichess signed out (it asks you to sign in):
    // the explorer's lists change, nothing around them moves.
    const QStringList lists{QStringLiteral("explorerMoves"), QStringLiteral("explorerGames")};
    QPushButton* bullet = nullptr;
    for (auto* button : window->findChildren<QPushButton*>()) {
        if (button->text() == u"Bullet") {
            bullet = button;
        }
    }
    ASSERT_NE(bullet, nullptr);
    bullet->click();
    const Layout filtered = now();
    EXPECT_TRUE(moved(database, filtered, lists).isEmpty())
        << text(moved(database, filtered, lists));
    bullet->click();
    for (const char* view : {"explorerShowGames", "explorerShowFilter", "explorerShowMoves"}) {
        QTest::mouseClick(window->findChild<QPushButton*>(QString::fromLatin1(view)),
                          Qt::LeftButton);
        const Layout shown = now();
        EXPECT_TRUE(moved(database, shown, lists).isEmpty()) << view << "\n"
                                                             << text(moved(database, shown, lists));
    }
    for (const char* key : {"lichess:lichess", "lichess:masters"}) {
        choose(source->findData(QString::fromLatin1(key)));
        const Layout online = now();
        EXPECT_TRUE(moved(database, online, lists).isEmpty())
            << key << "\n"
            << text(moved(database, online, lists));
    }
    choose(source->findData(QStringLiteral("db:"), Qt::MatchStartsWith));
    QSettings().remove(QStringLiteral("explorer/source"));
}

TEST_F(LayoutTest, TheExplorerStaysPutWhenTheTablebaseTakesOver) {
    addDatabase();
    // Eight pieces, then a capture: seven, and the tablebase answers in the moves' place.
    test::FakeServer server;
    server.pages["/standard"] =
        R"({"category":"draw","dtz":0,"dtm":null,"moves":[{"uci":"g3f3","san":"Kf3",)"
        R"("category":"draw","dtz":0,"dtm":null}]})";
    auto* explorer = window->findChild<app::ExplorerPanel*>(QStringLiteral("explorerPanel"));
    ASSERT_NE(explorer, nullptr);
    explorer->tablebase().setServer(server.url());
    auto* panels = window->findChild<QTabWidget*>(QStringLiteral("panels"));
    panels->setCurrentWidget(explorer);
    open("ending.pgn", "[FEN \"4k3/pp6/8/8/8/8/PP6/4K2r w - - 0 1\"]\n[SetUp \"1\"]\n\n"
                       "1. Kf2 Rh2+ 2. Kg3 Rxb2 *\n");
    for (int k = 0; k < 3; ++k) {
        QTest::keyClick(board, Qt::Key_Right);
    }
    const Layout eight = now();
    QTest::keyClick(board, Qt::Key_Right); // 2... Rxb2: seven pieces
    ASSERT_TRUE(QTest::qWaitFor([&] { return !explorer->tablebaseTexts().isEmpty(); }, 5000));
    const Layout seven = now();
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        window->grab().save(QDir(dir).filePath(QStringLiteral("explorer-tablebase.png")));
    }
    const QStringList lists{QStringLiteral("explorerMoves"), QStringLiteral("explorerTablebase")};
    EXPECT_TRUE(moved(eight, seven, lists).isEmpty()) << text(moved(eight, seven, lists));
    QTest::keyClick(board, Qt::Key_Left);
    const Layout back = now();
    EXPECT_TRUE(moved(seven, back, lists).isEmpty()) << text(moved(seven, back, lists));
}

// ---- The Library -------------------------------------------------------------------------------

TEST_F(LayoutTest, TheLibraryStaysPutWhateverIsSelected) {
    QDir(folder.path()).mkpath(QStringLiteral("OTB"));
    write("OTB/old.pgn", kClub);
    write("games.pgn", kClub);
    write("rep.pgn", "[FileType \"Repertoire\"]\n[Orientation \"white\"]\n\n"
                     "1. e4 e5 2. Nf3 (2. Bc4 Nf6) 2... Nc6 3. Bb5 *\n");
    write("tactics.pgn", "[FileType \"Puzzle\"]\n[FEN \"6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1\"]"
                         "\n[SetUp \"1\"]\n\n1. Ra8# *\n");
    QSignalSpy scanned(&window->library(), &app::Library::scanned);
    window->library().setFolder(folder.path());
    ASSERT_TRUE(scanned.wait(5000));
    window->showSection(MainWindow::Section::Library);
    auto* tree = window->findChild<QTreeWidget*>(QStringLiteral("libraryFiles"));
    auto* games = window->findChild<QTableView*>(QStringLiteral("libraryGames"));
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(games, nullptr);
    const auto select = [&](const char* name) {
        const auto found =
            tree->findItems(QString::fromLatin1(name), Qt::MatchExactly | Qt::MatchRecursive);
        ASSERT_FALSE(found.isEmpty()) << name;
        tree->setCurrentItem(found.front());
    };
    select("games");
    const Layout first = now();
    // What the right side shows changes with the file: its page, not where things are.
    const QStringList detail{QStringLiteral("libraryDetail")};
    for (const char* name : {"rep", "tactics", "OTB", "old", "games"}) {
        select(name);
        const Layout shown = now();
        EXPECT_TRUE(moved(first, shown, detail).isEmpty()) << name << "\n"
                                                           << text(moved(first, shown, detail));
    }
    games->setCurrentIndex(games->model()->index(2, 1));
    const Layout third = now();
    EXPECT_TRUE(moved(first, third, detail).isEmpty()) << text(moved(first, third, detail));
}

// ---- Databases and Profiles ---------------------------------------------------------------------

TEST_F(LayoutTest, TheDatabasesPageStaysPutWhileSearching) {
    addDatabase();
    window->showSection(MainWindow::Section::Databases);
    auto* list = window->findChild<QListWidget*>(QStringLiteral("databaseList"));
    ASSERT_NE(list, nullptr);
    list->setCurrentRow(0);
    const Layout all = now();
    // The games found change; the page around them doesn't.
    const QStringList games{QStringLiteral("databaseGames")};
    auto* search = window->findChild<QLineEdit*>(QStringLiteral("databaseSearch"));
    for (const char* typed : {"Carlsen", "nobody at all", ""}) {
        search->setText(QString::fromLatin1(typed));
        QTest::qWait(600); // the search starts once typing stops
        const Layout found = now();
        EXPECT_TRUE(moved(all, found, games).isEmpty()) << typed << "\n"
                                                        << text(moved(all, found, games));
    }
}

TEST_F(LayoutTest, TheProfilesPageStaysPutWhileSyncingSearchingAndChoosing) {
    auto* store = window->findChild<app::ProfileStore*>();
    auto* view = window->findChild<app::ProfilesView*>();
    ASSERT_NE(store, nullptr);
    ASSERT_NE(view, nullptr);
    const QString magnus = store->addProfile(QStringLiteral("Magnus"));
    store->addOtbGames(magnus, {write("magnus.pgn", kClub)});
    ASSERT_TRUE(QTest::qWaitFor([&] { return !store->busy(magnus); }, 15000));
    window->showSection(MainWindow::Section::Profiles);
    view->select(magnus);
    const Layout idle = now();
    // A sync: its status shows (in the summary's place) and goes.
    const QStringList overview{QStringLiteral("profileTheory")};
    store->sync(magnus);
    const Layout syncing = now();
    EXPECT_TRUE(moved(idle, syncing, overview).isEmpty()) << text(moved(idle, syncing, overview));
    ASSERT_TRUE(QTest::qWaitFor([&] { return !store->busy(magnus); }, 15000));
    // The games, searched: "Searching…" and "nothing found" leave the search where it is.
    QTest::mouseClick(window->findChild<QPushButton*>(QStringLiteral("profileShowGames")),
                      Qt::LeftButton);
    const Layout games = now();
    const QStringList list{QStringLiteral("profileGames")};
    auto* search = window->findChild<QLineEdit*>(QStringLiteral("profileSearch"));
    for (const char* typed : {"Carlsen", "nobody at all", ""}) {
        search->setText(QString::fromLatin1(typed));
        QTest::qWait(600);
        const Layout found = now();
        EXPECT_TRUE(moved(games, found, list).isEmpty()) << typed << "\n"
                                                         << text(moved(games, found, list));
    }
    store->removeProfile(magnus);
}

// ---- The other pages: each, then back to the board
// -----------------------------------------------

TEST_F(LayoutTest, SwitchingPagesAndBackLeavesTheBoardPageAsItWas) {
    const Layout board = now();
    for (const auto section : {MainWindow::Section::Library, MainWindow::Section::Puzzles,
                               MainWindow::Section::Databases, MainWindow::Section::Profiles,
                               MainWindow::Section::Engines, MainWindow::Section::Settings}) {
        window->showSection(section);
        settle();
        window->showSection(MainWindow::Section::Board);
        const Layout back = now();
        EXPECT_TRUE(moved(board, back).isEmpty()) << static_cast<int>(section) << "\n"
                                                  << text(moved(board, back));
    }
}

} // namespace
