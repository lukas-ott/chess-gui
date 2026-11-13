// The Databases page: importing PGN (plain and in archives) in the background, cancelling,
// browsing, searching and opening games.

#include "databases/AddDatabaseDialog.hpp"
#include "databases/DatabaseManager.hpp"
#include "databases/DatabasesView.hpp"
#include "explorer/ExplorerPanel.hpp"
#include "library/Library.hpp"
#include "profiles/LichessLogin.hpp"
#include "profiles/ProfileStore.hpp"
#include "repertoire/RepertoireStore.hpp"
#include "style/AppStyle.hpp"
#include "widgets/DateRange.hpp"

#include <QApplication>
#include <QCalendarWidget>
#include <QComboBox>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <gtest/gtest.h>

namespace {

using kiebitz::app::DatabaseManager;
using kiebitz::app::DatabasesView;

const char* const kGames = R"([Event "Club"]
[White "Carlsen, Magnus"]
[Black "Caruana, Fabiano"]
[Result "1-0"]

1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 1-0

[Event "Open"]
[White "Smith"]
[Black "Carlsen, Magnus"]
[Result "0-1"]

1. e4 c5 2. Nf3 d6 0-1

[Event "Open"]
[White "Jones"]
[Black "Brown"]
[Result "1/2-1/2"]

1. d4 d5 2. c4 e6 1/2-1/2

[Event "Blitz"]
[White "A"]
[Black "B"]
[Result "*"]

1. e4 e5 2. Nf3 Nf6 *
)";

void write(const QString& path, const QByteArray& text) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(text);
}

/// Imports and waits; returns the error ("" on success).
QString importAndWait(DatabaseManager& manager, const QString& path, const QString& name) {
    QSignalSpy finished(&manager, &DatabaseManager::jobFinished);
    QSignalSpy changed(&manager, &DatabaseManager::changed);
    manager.importFile(path, name);
    if (!finished.wait(20000)) {
        return QStringLiteral("timed out");
    }
    const QString error = finished.front().at(1).toString();
    if (error.isEmpty()) {
        changed.wait(5000); // the list, read again
    }
    return error;
}

class DatabasesTest : public ::testing::Test {
protected:
    QTemporaryDir folder;
    QString path(const char* name) const { return folder.filePath(QString::fromLatin1(name)); }
};

} // namespace

TEST_F(DatabasesTest, ImportsPgnAndArchives) {
    DatabaseManager manager(path("dbs"));
    write(path("games.pgn"), kGames);
    EXPECT_EQ(importAndWait(manager, path("games.pgn"), QStringLiteral("Club games")), QString());
    ASSERT_EQ(manager.databases().size(), 1U);
    EXPECT_EQ(manager.databases()[0].db->gameCount(), 4U);
    EXPECT_EQ(manager.databases()[0].db->info().name, "Club games");

    // A zip with a readme next to the PGN: only the PGN is read.
    QDir().mkpath(path("zip"));
    write(path("zip/games.pgn"), kGames);
    write(path("zip/README.txt"), "Not a game.\n");
    QProcess zip;
    zip.setWorkingDirectory(path("zip"));
    zip.start(QStringLiteral("/usr/bin/zip"),
              {QStringLiteral("-q"), path("games.zip"), QStringLiteral("games.pgn"),
               QStringLiteral("README.txt")});
    ASSERT_TRUE(zip.waitForFinished());
    EXPECT_EQ(importAndWait(manager, path("games.zip"), QStringLiteral("Zipped")), QString());
    ASSERT_EQ(manager.databases().size(), 2U);
    EXPECT_EQ(manager.databases()[1].db->info().name, "Zipped");
    EXPECT_EQ(manager.databases()[1].db->gameCount(), 4U);

    // Nothing in it: an error, and no database.
    write(path("empty.pgn"), "\n\n");
    EXPECT_FALSE(importAndWait(manager, path("empty.pgn"), QStringLiteral("Empty")).isEmpty());
    EXPECT_EQ(manager.databases().size(), 2U);
    EXPECT_TRUE(QDir(path("dbs")).entryList({QStringLiteral("*.part")}, QDir::Dirs).isEmpty());
}

TEST_F(DatabasesTest, CancellingLeavesNothingBehind) {
    DatabaseManager manager(path("dbs"));
    QByteArray big;
    for (int i = 0; i < 20000; ++i) {
        big += kGames;
    }
    write(path("big.pgn"), big);
    QSignalSpy finished(&manager, &DatabaseManager::jobFinished);
    const int job = manager.importFile(path("big.pgn"), QStringLiteral("Big"));
    EXPECT_EQ(manager.jobs().size(), 1U);
    manager.cancel(job);
    ASSERT_TRUE(finished.wait(20000));
    EXPECT_EQ(finished.front().at(1).toString(), QStringLiteral("cancelled"));
    EXPECT_TRUE(manager.jobs().empty());
    EXPECT_TRUE(QDir(path("dbs")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
}

TEST_F(DatabasesTest, ThePageListsSearchesAndOpensGames) {
    const kiebitz::app::AppStyle style(*qApp);
    DatabaseManager manager(path("dbs"));
    DatabasesView view(manager, style);
    view.resize(1100, 700);
    view.show();
    write(path("games.pgn"), kGames);
    ASSERT_EQ(importAndWait(manager, path("games.pgn"), QStringLiteral("Club games")), QString());
    QApplication::processEvents();

    auto* list = view.findChild<QListWidget*>(QStringLiteral("databaseList"));
    auto* games = view.findChild<QTableView*>(QStringLiteral("databaseGames"));
    ASSERT_NE(list, nullptr);
    ASSERT_NE(games, nullptr);
    EXPECT_EQ(list->count(), 1);
    EXPECT_EQ(games->model()->rowCount(), 4); // all of them, in file order
    EXPECT_EQ(games->model()->index(1, 0).data().toString(), QStringLiteral("Smith"));
    EXPECT_EQ(view.findChild<QLabel*>(QStringLiteral("databaseTitle"))->text(),
              QStringLiteral("Club games"));

    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        QTest::qWait(100);
        view.grab().save(QDir(dir).filePath(QStringLiteral("databases.png")));
        kiebitz::app::AddDatabaseDialog dialog;
        dialog.show();
        QTest::qWait(100);
        dialog.grab().save(QDir(dir).filePath(QStringLiteral("add-database.png")));
    }
    // Search: Carlsen's two games.
    view.findChild<QLineEdit*>(QStringLiteral("databaseSearch"))
        ->setText(QStringLiteral("carlsen"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return games->model()->rowCount() == 2; }, 5000));

    // Double-click: the game, for the board.
    QSignalSpy opened(&view, &DatabasesView::openGameRequested);
    const QRect cell = games->visualRect(games->model()->index(0, 0));
    QTest::mouseClick(games->viewport(), Qt::LeftButton, {}, cell.center()); // as a real one
    QTest::mouseDClick(games->viewport(), Qt::LeftButton, {}, cell.center());
    ASSERT_EQ(opened.size(), 1);
    const auto game = opened.front().at(0).value<kiebitz::core::Game>();
    EXPECT_FALSE(std::string(game.header("White")).empty());
}

TEST_F(DatabasesTest, ColumnsSortAndDatesFilter) {
    const kiebitz::app::AppStyle style(*qApp);
    DatabaseManager manager(path("dbs"));
    DatabasesView view(manager, style);
    view.resize(1100, 700);
    view.show();
    write(path("dated.pgn"), "[White \"Carol\"]\n[Black \"x\"]\n[Date \"2021.03.01\"]\n"
                             "[Result \"1-0\"]\n\n1. e4 e5 1-0\n\n"
                             "[White \"alice\"]\n[Black \"x\"]\n[Date \"2023.07.15\"]\n"
                             "[Result \"0-1\"]\n\n1. d4 d5 0-1\n\n"
                             "[White \"Bob\"]\n[Black \"x\"]\n[Date \"2023.01.02\"]\n"
                             "[Result \"1-0\"]\n\n1. c4 c5 1-0\n");
    ASSERT_EQ(importAndWait(manager, path("dated.pgn"), QStringLiteral("Dated")), QString());
    QApplication::processEvents();
    auto* games = view.findChild<QTableView*>(QStringLiteral("databaseGames"));
    ASSERT_NE(games, nullptr);
    const auto whites = [&] {
        QStringList names;
        for (int row = 0; row < games->model()->rowCount(); ++row) {
            names << games->model()->index(row, 0).data().toString();
        }
        return names;
    };
    ASSERT_EQ(whites(), (QStringList{"Carol", "alice", "Bob"})); // as stored
    // A click on WHITE's title: by name; again: the other way round.
    const auto* header = games->horizontalHeader();
    const QPoint white(header->sectionViewportPosition(0) + 10, header->height() / 2);
    QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, white);
    ASSERT_TRUE(
        QTest::qWaitFor([&] { return whites() == QStringList{"alice", "Bob", "Carol"}; }, 5000))
        << whites().join(u',').toStdString();
    QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, white);
    ASSERT_TRUE(
        QTest::qWaitFor([&] { return whites() == QStringList{"Carol", "Bob", "alice"}; }, 5000));
    // A third click: as stored again, without an arrow.
    QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, white);
    ASSERT_TRUE(
        QTest::qWaitFor([&] { return whites() == QStringList{"Carol", "alice", "Bob"}; }, 5000));
    EXPECT_EQ(header->sortIndicatorSection(), -1);
    // Since 2023: Carol's 2021 game goes; up to June 2023: alice's July game too.
    auto* dates = view.findChild<kiebitz::app::DateRange*>();
    ASSERT_NE(dates, nullptr);
    EXPECT_EQ(dates->text(), QStringLiteral("Any date"));
    dates->setRange(QDate(2023, 1, 1), std::nullopt);
    ASSERT_TRUE(QTest::qWaitFor([&] { return whites() == QStringList{"alice", "Bob"}; }, 5000));
    dates->setRange(QDate(2023, 1, 1), QDate(2023, 6, 30));
    ASSERT_TRUE(QTest::qWaitFor([&] { return whites() == QStringList{"Bob"}; }, 5000));
    // The picker: a calendar for each end, and shortcuts; "Any time" takes the range away.
    QTest::mouseClick(view.findChild<QPushButton*>(QStringLiteral("databaseDateButton")),
                      Qt::LeftButton);
    QWidget* picker = nullptr;
    for (QWidget* top : QApplication::topLevelWidgets()) {
        if (top->objectName() == u"dateRangePicker" && top->isVisible()) {
            picker = top;
        }
    }
    ASSERT_NE(picker, nullptr);
    EXPECT_NE(picker->findChild<QCalendarWidget*>(QStringLiteral("dateRangeFrom")), nullptr);
    EXPECT_NE(picker->findChild<QCalendarWidget*>(QStringLiteral("dateRangeTo")), nullptr);
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        QTest::qWait(100);
        picker->grab().save(QDir(dir).filePath(QStringLiteral("date-picker.png")));
    }
    // A day picked in the To calendar: the range ends there.
    auto* toCalendar = picker->findChild<QCalendarWidget*>(QStringLiteral("dateRangeTo"));
    emit toCalendar->clicked(QDate(2023, 12, 31));
    ASSERT_TRUE(QTest::qWaitFor([&] { return whites() == QStringList{"alice", "Bob"}; }, 5000));
    EXPECT_EQ(dates->text(), QStringLiteral("2023")); // a whole year reads as the year
    for (auto* chip : picker->findChildren<QPushButton*>()) {
        if (chip->text() == u"Any time") {
            chip->click();
        }
    }
    ASSERT_TRUE(QTest::qWaitFor([&] { return whites().size() == 3; }, 5000));
    EXPECT_EQ(dates->text(), QStringLiteral("Any date"));
}

TEST_F(DatabasesTest, EachExplorerSourceKeepsItsOwnFilter) {
    const kiebitz::app::AppStyle style(*qApp);
    DatabaseManager manager(path("dbs"));
    kiebitz::app::Library library;
    kiebitz::app::RepertoireStore repertoires(library);
    kiebitz::app::ProfileStore profiles(path("profiles"));
    kiebitz::app::LichessLogin lichess{kiebitz::app::LichessLogin::Storage{}};
    kiebitz::app::ExplorerPanel explorer(manager, profiles, repertoires, lichess, style);
    explorer.show();
    write(path("games.pgn"), kGames);
    ASSERT_EQ(importAndWait(manager, path("games.pgn"), QStringLiteral("Club games")), QString());
    const kiebitz::core::Game game;
    QSignalSpy updated(&explorer, &kiebitz::app::ExplorerPanel::updated);
    explorer.setPosition(&game, kiebitz::core::Game::root(), kiebitz::core::Color::White);
    ASSERT_TRUE(updated.wait(5000));
    ASSERT_FALSE(explorer.moveTexts().isEmpty());

    auto* source = explorer.findChild<QComboBox*>(QStringLiteral("explorerSource"));
    const auto* filterTab = explorer.findChild<QPushButton*>(QStringLiteral("explorerShowFilter"));
    QPushButton* bullet = nullptr;
    for (auto* button : explorer.findChildren<QPushButton*>()) {
        if (button->text() == u"Bullet") {
            bullet = button;
        }
    }
    ASSERT_NE(bullet, nullptr);
    const auto choose = [&](const QString& key) {
        source->setCurrentIndex(source->findData(key));
        emit source->activated(source->currentIndex());
    };
    const QString database = source->currentData().toString();
    ASSERT_TRUE(database.startsWith(u"db:"));

    // Bullet for Lichess: the database's games stay, unfiltered.
    choose(QStringLiteral("lichess:lichess"));
    EXPECT_FALSE(bullet->isChecked());
    bullet->click();
    EXPECT_TRUE(filterTab->text().contains(u"•"));
    choose(database);
    EXPECT_FALSE(bullet->isChecked());
    EXPECT_FALSE(filterTab->text().contains(u"•"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return !explorer.moveTexts().isEmpty(); }, 5000));

    // Back on Lichess, its filter is still there; and in a new window too (remembered).
    choose(QStringLiteral("lichess:lichess"));
    EXPECT_TRUE(bullet->isChecked());
    const kiebitz::app::ExplorerPanel again(manager, profiles, repertoires, lichess, style);
    QPushButton* bulletAgain = nullptr;
    for (auto* button : again.findChildren<QPushButton*>()) {
        if (button->text() == u"Bullet") {
            bulletAgain = button;
        }
    }
    ASSERT_NE(bulletAgain, nullptr);
    EXPECT_TRUE(bulletAgain->isChecked()); // it opens on Lichess, the source chosen last
    bullet->click();                       // the other tests start without a filter
    QSettings().remove(QStringLiteral("explorer/source"));
}

TEST(AddDatabase, AChosenFileNamesTheDatabase) {
    kiebitz::app::AddDatabaseDialog dialog;
    dialog.chooseFile(QStringLiteral("/tmp/Caissabase 2024.7z"));
    EXPECT_EQ(dialog.name(), QStringLiteral("Caissabase 2024"));
    EXPECT_EQ(dialog.path(), QStringLiteral("/tmp/Caissabase 2024.7z"));
}

TEST_F(DatabasesTest, TheExplorerShowsWhatIsPlayedAndPlaysAMove) {
    const kiebitz::app::AppStyle style(*qApp);
    DatabaseManager manager(path("dbs"));
    kiebitz::app::Library library;
    kiebitz::app::RepertoireStore repertoires(library);
    kiebitz::app::ProfileStore profiles(path("profiles"));
    kiebitz::app::LichessLogin lichess{kiebitz::app::LichessLogin::Storage{}};
    kiebitz::app::ExplorerPanel explorer(manager, profiles, repertoires, lichess, style);
    explorer.resize(560, 300); // as short as the tab under the board can be
    explorer.show();
    write(path("games.pgn"), kGames);
    ASSERT_EQ(importAndWait(manager, path("games.pgn"), QStringLiteral("Club games")), QString());

    // After 1. e4: 1... e5 twice, 1... c5 once (most played first).
    kiebitz::core::Game game;
    const kiebitz::core::Position start;
    const auto e4 = game.addMove(kiebitz::core::Game::root(), start.parseSan("e4").value());
    QSignalSpy updated(&explorer, &kiebitz::app::ExplorerPanel::updated);
    explorer.setPosition(&game, e4, kiebitz::core::Color::White);
    ASSERT_TRUE(updated.wait(5000));
    const QStringList moves = explorer.moveTexts();
    ASSERT_EQ(moves.size(), 2);
    EXPECT_TRUE(moves[0].startsWith(u"e5 2"));
    EXPECT_TRUE(moves[1].startsWith(u"c5 1"));
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        QTest::qWait(100);
        explorer.grab().save(QDir(dir).filePath(QStringLiteral("explorer.png")));
    }

    // A click plays the move.
    auto* table = explorer.findChild<QTreeWidget*>(QStringLiteral("explorerMoves"));
    ASSERT_NE(table, nullptr);
    QSignalSpy played(&explorer, &kiebitz::app::ExplorerPanel::movePlayed);
    const QRect row = table->visualItemRect(table->topLevelItem(0));
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, row.center());
    ASSERT_EQ(played.size(), 1);
    EXPECT_EQ(played.front().at(0).value<kiebitz::core::Move>().uci(), "e7e5");
}

TEST_F(DatabasesTest, ADoubleClickOnATopGameOpensIt) {
    const kiebitz::app::AppStyle style(*qApp);
    DatabaseManager manager(path("dbs"));
    kiebitz::app::Library library;
    kiebitz::app::RepertoireStore repertoires(library);
    kiebitz::app::ProfileStore profiles(path("profiles"));
    kiebitz::app::LichessLogin lichess{kiebitz::app::LichessLogin::Storage{}};
    kiebitz::app::ExplorerPanel explorer(manager, profiles, repertoires, lichess, style);
    explorer.resize(560, 300);
    explorer.show();
    write(path("games.pgn"), kGames);
    ASSERT_EQ(importAndWait(manager, path("games.pgn"), QStringLiteral("Club games")), QString());
    const kiebitz::core::Game game;
    QSignalSpy updated(&explorer, &kiebitz::app::ExplorerPanel::updated);
    explorer.setPosition(&game, kiebitz::core::Game::root(), kiebitz::core::Color::White);
    ASSERT_TRUE(updated.wait(5000));
    QTest::mouseClick(explorer.findChild<QPushButton*>(QStringLiteral("explorerShowGames")),
                      Qt::LeftButton);
    auto* games = explorer.findChild<QTreeWidget*>(QStringLiteral("explorerGames"));
    ASSERT_NE(games, nullptr);
    ASSERT_TRUE(games->isVisible());
    ASSERT_GT(games->topLevelItemCount(), 0);
    const QSignalSpy opened(&explorer, &kiebitz::app::ExplorerPanel::openGameRequested);
    const QPoint first = games->visualItemRect(games->topLevelItem(0)).center();
    QTest::mouseClick(games->viewport(), Qt::LeftButton, {}, first);
    EXPECT_EQ(opened.size(), 0); // one click only selects
    QTest::mouseDClick(games->viewport(), Qt::LeftButton, {}, first);
    EXPECT_EQ(opened.size(), 1);
}
