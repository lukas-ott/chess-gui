// Profiles: accounts whose games are downloaded (from a fake Lichess and chess.com, never the
// real ones), over-the-board games added by hand, and the databases built from them.

#include "databases/DatabaseManager.hpp"
#include "explorer/ExplorerPanel.hpp"
#include "library/Library.hpp"
#include "profiles/Fide.hpp"
#include "profiles/ProfileStore.hpp"
#include "profiles/ProfilesView.hpp"
#include "repertoire/RepertoireStore.hpp"
#include "style/AppStyle.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <map>

namespace {

using kiebitz::app::AccountSite;
using kiebitz::app::ProfileStore;

QByteArray game(const char* white, const char* black, const char* moves) {
    return QByteArray("[Site \"https://lichess.org/x\"]\n[Date \"2025.06.01\"]\n[White \"") +
           white + "\"]\n[Black \"" + black + "\"]\n[Result \"1-0\"]\n\n" + moves + " 1-0\n\n";
}

/// Answers GET requests from a table of paths; remembers what was asked.
class FakeServer : public QObject {
public:
    FakeServer() {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* socket = m_server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                const QByteArray request = socket->readAll();
                const QByteArray target = request.split(' ').value(1);
                asked << QString::fromUtf8(target);
                const QByteArray path = target.split('?').value(0);
                const auto found = pages.find(path);
                const QByteArray body = found != pages.end() ? found->second : "Not found";
                socket->write(
                    (found != pages.end() ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n") +
                    QByteArray("Content-Length: ") + QByteArray::number(body.size()) +
                    "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }
    [[nodiscard]] QUrl url() const {
        return {QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort())};
    }
    std::map<QByteArray, QByteArray> pages;
    QStringList asked;

private:
    QTcpServer m_server;
};

class ProfilesTest : public ::testing::Test {
protected:
    QTemporaryDir folder;
    FakeServer server;

    static void waitFor(const ProfileStore& store, const QString& id) {
        ASSERT_TRUE(QTest::qWaitFor([&] { return !store.busy(id); }, 15000));
    }
};

} // namespace

TEST_F(ProfilesTest, ThereIsAlwaysAMainProfile) {
    const ProfileStore store(folder.path());
    ASSERT_EQ(store.profiles().size(), 1U);
    EXPECT_TRUE(store.profiles()[0].main);
}

TEST_F(ProfilesTest, LichessGamesAreDownloadedAndThenOnlyNewOnes) {
    server.pages["/api/games/user/alice"] =
        game("alice", "bob", "1. e4 e5 2. Nf3") + game("carol", "alice", "1. d4 d5");
    server.pages["/api/user/alice"] =
        R"({"title":"FM","perfs":{"blitz":{"games":120,"rating":2210,"prov":false},)"
        R"("bullet":{"games":3,"rating":1500,"prov":true}}})";
    ProfileStore store(folder.path());
    store.setServers(server.url(), server.url(), server.url());
    const QString me = store.profiles()[0].id;
    EXPECT_FALSE(store.addAccount(me, AccountSite::Lichess, QStringLiteral("alice")).isEmpty());
    waitFor(store, me);
    const auto* profile = store.profile(me);
    ASSERT_NE(profile->all, nullptr);
    EXPECT_EQ(profile->all->gameCount(), 2U);
    ASSERT_EQ(profile->parts.count(QStringLiteral("lichess-alice")), 1U);
    EXPECT_FALSE(profile->accounts[0].synced.isNull());
    EXPECT_FALSE(server.asked.contains(QStringLiteral("/api/games/user/alice?since"))); // all
    // Ratings from the site (a provisional one is left out), and the title.
    EXPECT_EQ(profile->accounts[0].title, QStringLiteral("FM"));
    EXPECT_EQ(profile->accounts[0].ratings.at(QStringLiteral("blitz")), 2210);
    EXPECT_FALSE(profile->accounts[0].ratings.contains(QStringLiteral("bullet")));
    // The record by colour: won with White (vs bob), lost with Black (vs carol).
    ASSERT_TRUE(QTest::qWaitFor([&] { return store.profile(me)->records.contains({}); }, 5000));
    const auto& record = store.profile(me)->records.at({});
    EXPECT_EQ(record.asWhite, (std::array<std::uint64_t, 3>{1, 0, 0}));
    EXPECT_EQ(record.asBlack, (std::array<std::uint64_t, 3>{0, 0, 1}));

    // The next sync asks only for games since the last one.
    server.pages["/api/games/user/alice"] = game("alice", "dave", "1. c4 c5");
    store.sync(me);
    waitFor(store, me);
    EXPECT_TRUE(std::any_of(server.asked.begin(), server.asked.end(), [](const QString& asked) {
        return asked.startsWith(u"/api/games/user/alice?since=");
    }));
    EXPECT_EQ(store.profile(me)->all->gameCount(), 3U);
    // The same account twice: not added again.
    EXPECT_TRUE(store.addAccount(me, AccountSite::Lichess, QStringLiteral("Alice")).isEmpty());
}

TEST_F(ProfilesTest, ChessComMonthsAreDownloadedOnce) {
    const QByteArray base = server.url().toString().toUtf8();
    server.pages["/pub/player/bob/games/archives"] = R"({"archives":[")" + base +
                                                     R"(/pub/player/bob/games/2026/09",")" + base +
                                                     R"(/pub/player/bob/games/2026/10"]})";
    server.pages["/pub/player/bob/games/2026/09/pgn"] = game("Bob", "x", "1. e4 c5");
    server.pages["/pub/player/bob/games/2026/10/pgn"] = game("Bob", "y", "1. d4 Nf6");
    ProfileStore store(folder.path());
    store.setServers(server.url(), server.url(), server.url());
    const QString me = store.profiles()[0].id;
    store.addAccount(me, AccountSite::ChessCom, QStringLiteral("Bob"));
    waitFor(store, me);
    EXPECT_EQ(store.profile(me)->all->gameCount(), 2U);
    // Again: September is complete, only October (still being played) is asked for.
    server.asked.clear();
    server.pages["/pub/player/bob/games/2026/10/pgn"] =
        game("Bob", "y", "1. d4 Nf6") + game("Bob", "z", "1. Nf3 d5");
    store.sync(me);
    waitFor(store, me);
    EXPECT_FALSE(server.asked.contains(QStringLiteral("/pub/player/bob/games/2026/09/pgn")));
    EXPECT_TRUE(server.asked.contains(QStringLiteral("/pub/player/bob/games/2026/10/pgn")));
    EXPECT_EQ(store.profile(me)->all->gameCount(), 3U);
}

TEST_F(ProfilesTest, OverTheBoardGamesAndOtherProfiles) {
    ProfileStore store(folder.path());
    store.setServers(server.url(), server.url(), server.url());
    const QString friendId = store.addProfile(QStringLiteral("Magnus"));
    ASSERT_EQ(store.profiles().size(), 2U);
    QFile pgn(folder.filePath(QStringLiteral("club.pgn")));
    ASSERT_TRUE(pgn.open(QIODevice::WriteOnly));
    pgn.write(game("Magnus", "Hikaru", "1. e4 e5") + game("Hikaru", "Magnus", "1. d4 d5"));
    pgn.close();
    store.addOtbGames(friendId, {pgn.fileName()});
    waitFor(store, friendId);
    const auto* profile = store.profile(friendId);
    ASSERT_NE(profile->all, nullptr);
    EXPECT_EQ(profile->all->gameCount(), 2U);
    ASSERT_EQ(profile->parts.count(QStringLiteral("otb")), 1U);

    // An account that doesn't exist: said, and nothing breaks.
    QSignalSpy failed(&store, &ProfileStore::failed);
    store.addAccount(friendId, AccountSite::Lichess, QStringLiteral("nobody"));
    waitFor(store, friendId);
    ASSERT_EQ(failed.size(), 1);
    EXPECT_TRUE(failed.front().at(1).toString().contains(u"no Lichess account"));
    EXPECT_EQ(store.profile(friendId)->all->gameCount(), 2U);

    // Everything is still there after a restart.
    const ProfileStore again(folder.path());
    ASSERT_EQ(again.profiles().size(), 2U);
    const auto* loaded = again.profile(friendId);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->name, QStringLiteral("Magnus"));
    ASSERT_NE(loaded->all, nullptr);
    EXPECT_EQ(loaded->all->gameCount(), 2U);
}

TEST_F(ProfilesTest, ThePageShowsAProfileAndItsPartsAndTheExplorerUsesThem) {
    server.pages["/api/games/user/alice"] =
        game("alice", "bob", "1. e4 e5 2. Nf3") + game("carol", "alice", "1. d4 d5");
    server.pages["/api/user/alice"] =
        R"({"title":"FM","count":{"all":5},"perfs":{"blitz":{"games":120,"rating":2210,)"
        R"("prov":false},"rapid":{"games":40,"rating":2150,"prov":false}}})";
    const kiebitz::app::AppStyle style(*qApp);
    ProfileStore store(folder.filePath(QStringLiteral("profiles")));
    store.setServers(server.url(), server.url(), server.url());
    kiebitz::app::ProfilesView view(store, style);
    view.resize(1180, 720);
    view.show();
    const QString me = store.profiles()[0].id;
    store.addAccount(me, AccountSite::Lichess, QStringLiteral("alice"));
    waitFor(store, me);
    QFile pgn(folder.filePath(QStringLiteral("club.pgn")));
    ASSERT_TRUE(pgn.open(QIODevice::WriteOnly));
    pgn.write(game("alice", "Hikaru", "1. c4 e5"));
    pgn.close();
    store.addOtbGames(me, {pgn.fileName()});
    waitFor(store, me);
    QApplication::processEvents();

    // All three games; then only the over-the-board one.
    auto* games = view.findChild<QTableView*>(QStringLiteral("profileGames"));
    ASSERT_NE(games, nullptr);
    EXPECT_EQ(games->model()->rowCount(), 3);
    // A dropdown with all games, Lichess · alice and over the board.
    auto* part = view.findChild<QComboBox*>(QStringLiteral("profilePart"));
    ASSERT_NE(part, nullptr);
    EXPECT_EQ(part->count(), 3);
    EXPECT_EQ(part->itemText(1), QStringLiteral("Lichess · alice"));
    part->setCurrentIndex(part->findData(QStringLiteral("otb")));
    emit part->activated(part->currentIndex());
    EXPECT_EQ(games->model()->rowCount(), 1);
    // The overview counts alice's games: two downloaded, one over the board.
    part->setCurrentIndex(0);
    emit part->activated(0);
    auto* total = view.findChild<QLabel*>(QStringLiteral("profileTotal"));
    ASSERT_NE(total, nullptr);
    EXPECT_TRUE(QTest::qWaitFor([&] { return total->text().startsWith(u"3 games"); }, 5000))
        << total->text().toStdString();
    store.addProfile(QStringLiteral("Maximilian Alexander von Habsburg-Lothringen"));
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        QTest::qWait(100);
        view.grab().save(QDir(dir).filePath(QStringLiteral("profiles.png")));
    }

    // The explorer offers the profile and its parts.
    kiebitz::app::DatabaseManager databases(folder.filePath(QStringLiteral("dbs")));
    kiebitz::app::Library library;
    kiebitz::app::RepertoireStore repertoires(library);
    kiebitz::app::ExplorerPanel explorer(databases, store, repertoires, style);
    auto* source = explorer.findChild<QComboBox*>(QStringLiteral("explorerSource"));
    ASSERT_NE(source, nullptr);
    EXPECT_GE(source->findData(QStringLiteral("profile:") + me), 0);
    EXPECT_EQ(source->findData(QStringLiteral("profile:") + me + QStringLiteral(":otb")), -1);
    // The profile is in the list once; its parts are in the Filter tab.
    source->setCurrentIndex(source->findData(QStringLiteral("profile:") + me));
    emit source->activated(source->currentIndex());
    const kiebitz::core::Game start;
    QSignalSpy updated(&explorer, &kiebitz::app::ExplorerPanel::updated);
    explorer.setPosition(&start, kiebitz::core::Game::root(), kiebitz::core::Color::White);
    ASSERT_TRUE(updated.wait(5000));
    EXPECT_EQ(explorer.moveTexts().size(), 3); // e4, d4, c4

    // Filter: only the games where the profile (alice) had White: e4 and c4.
    QTest::mouseClick(explorer.findChild<QPushButton*>(QStringLiteral("explorerShowFilter")),
                      Qt::LeftButton);
    auto* asWhite = explorer.findChild<QPushButton*>(QStringLiteral("explorerSide1"));
    ASSERT_NE(asWhite, nullptr);
    EXPECT_TRUE(asWhite->isVisibleTo(&explorer)); // a profile: the colour can be chosen
    asWhite->click();
    ASSERT_TRUE(updated.wait(5000));
    EXPECT_EQ(explorer.moveTexts().size(), 2);
    EXPECT_TRUE(explorer.findChild<QPushButton*>(QStringLiteral("explorerShowFilter"))
                    ->text()
                    .contains(u"•"));
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        explorer.resize(560, 320);
        explorer.show();
        QTest::qWait(100);
        explorer.grab().save(QDir(dir).filePath(QStringLiteral("explorer-filter.png")));
    }
    explorer.findChild<QPushButton*>(QStringLiteral("explorerFilterReset"))->click();
    ASSERT_TRUE(QTest::qWaitFor([&] { return explorer.moveTexts().size() == 3; }, 5000));

    // Games from: only the over-the-board one (c4).
    auto* from = explorer.findChild<QComboBox*>(QStringLiteral("explorerPart"));
    ASSERT_NE(from, nullptr);
    from->setCurrentIndex(from->findData(QStringLiteral("otb")));
    emit from->activated(from->currentIndex());
    ASSERT_TRUE(QTest::qWaitFor([&] { return explorer.moveTexts().size() == 1; }, 5000));
    EXPECT_TRUE(explorer.moveTexts().front().startsWith(u"c4"));
    // A player's name, for one colour: carol had White once (1. d4).
    explorer.findChild<QPushButton*>(QStringLiteral("explorerFilterReset"))->click();
    explorer.findChild<QLineEdit*>(QStringLiteral("explorerWhite"))->setText(QStringLiteral("CAR"));
    ASSERT_TRUE(QTest::qWaitFor(
        [&] {
            const QStringList moves = explorer.moveTexts();
            return moves.size() == 1 && moves.front().startsWith(u"d4");
        },
        5000));
    explorer.findChild<QPushButton*>(QStringLiteral("explorerFilterReset"))->click();
}

namespace {

/// Two rows as FIDE's rating search answers them.
const char* const kFideAnswer = R"(<table><thead><tr><th>FIDE ID</th></tr></thead><tbody><tr>
<td data-label="FIDEID" class="box-div">1503014</td>
<td data-label="Name"><a href=/profile/1503014 class="found_name">Carlsen, Magnus</a></td>
<td data-label="title">GM </td><td data-label="Train. Ttl."><span></span></td>
<td class="flag-wrapper"><img src="/svg/NOR.svg" alt="NOR">NOR</td>
<td data-label="Rtg">2823</td><td data-label="Rtg">2801</td><td data-label="Rtg">2860</td>
<td data-label="B-Year">1990</td></tr><tr>
<td data-label="FIDEID" class="box-div">99</td>
<td data-label="Name"><a href=/profile/99 class="found_name">Sanico, Magnus Carlsen</a></td>
<td data-label="title"> </td><td data-label="Train. Ttl."><span></span></td>
<td class="flag-wrapper"><img src="/svg/PHI.svg" alt="PHI">PHI</td>
<td data-label="Rtg">1500</td><td data-label="Rtg"></td><td data-label="Rtg"></td>
<td data-label="B-Year">2001</td></tr></tbody></table>)";

} // namespace

TEST(Fide, ReadsTheSearchAndRanksTheClosestNameFirst) {
    const auto players = kiebitz::app::parseFideSearch(QString::fromUtf8(kFideAnswer));
    ASSERT_EQ(players.size(), 2U);
    EXPECT_EQ(players[0].id, 1503014);
    EXPECT_EQ(players[0].title, QStringLiteral("GM"));
    EXPECT_EQ(players[0].federation, QStringLiteral("NOR"));
    EXPECT_EQ(players[0].standard, 2823);
    EXPECT_EQ(players[0].blitz, 2860);
    EXPECT_EQ(players[0].born, 1990);
    // "Magnus Carlsen" fits Carlsen, Magnus exactly; Sanico has a third name.
    const auto ranked =
        kiebitz::app::rankFideMatches({players[1], players[0]}, QStringLiteral("magnus CARLSEN"));
    ASSERT_EQ(ranked.size(), 2U);
    EXPECT_EQ(ranked[0].id, 1503014);
    EXPECT_EQ(kiebitz::app::rankFideMatches(players, QStringLiteral("Hikaru Nakamura")).size(), 0U);
}

TEST_F(ProfilesTest, AProfileFindsItsFidePlayerAndCanPickAnother) {
    server.pages["/incl_search_l.php"] = kFideAnswer;
    ProfileStore store(folder.path());
    store.setServers(server.url(), server.url(), server.url());
    const QString id = store.addProfile(QStringLiteral("Magnus Carlsen"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return store.profile(id)->fideLooked; }, 5000));
    EXPECT_TRUE(server.asked.back().contains(u"search=Magnus"));
    ASSERT_TRUE(store.profile(id)->fide.has_value());
    EXPECT_EQ(store.profile(id)->fide->id, 1503014);
    EXPECT_EQ(store.profile(id)->fideCandidates.size(), 2U);

    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        const kiebitz::app::AppStyle style(*qApp);
        kiebitz::app::ProfilesView view(store, style);
        view.resize(1180, 720);
        view.show();
        view.select(id);
        QTest::mouseClick(view.findChild<QPushButton*>(QStringLiteral("profileFide")),
                          Qt::LeftButton);
        QTest::qWait(150);
        for (QWidget* top : QApplication::topLevelWidgets()) {
            if (top->objectName() == u"fideCard" && top->isVisible()) {
                top->grab().save(QDir(dir).filePath(QStringLiteral("fide-card.png")));
                top->close();
            }
        }
        view.grab().save(QDir(dir).filePath(QStringLiteral("profile-fide.png")));
    }
    // Not them: the other one; then none at all. A lookup doesn't undo a choice.
    store.chooseFide(id, 99);
    EXPECT_EQ(store.profile(id)->fide->id, 99);
    store.chooseFide(id, 0);
    EXPECT_FALSE(store.profile(id)->fide.has_value());
    store.lookUpFide(id);
    QTest::qWait(300);
    EXPECT_FALSE(store.profile(id)->fide.has_value());

    // A one-word name isn't looked up ("Me", "Magnus" would find anyone).
    const int asked = static_cast<int>(server.asked.size());
    store.addProfile(QStringLiteral("Magnus"));
    QTest::qWait(200);
    EXPECT_EQ(server.asked.size(), asked);
}

TEST_F(ProfilesTest, WhenFideDoesNotAnswerItSaysSoAndCanTryAgain) {
    ProfileStore store(folder.path());
    store.setServers(server.url(), server.url(), server.url()); // no FIDE page: an error
    const QString id = store.addProfile(QStringLiteral("Magnus Carlsen"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return !store.profile(id)->fideError.isEmpty(); }, 5000));
    EXPECT_FALSE(store.profile(id)->fideLooked);      // so it is tried again next time
    server.pages["/incl_search_l.php"] = kFideAnswer; // FIDE is back
    store.lookUpFide(id);
    ASSERT_TRUE(QTest::qWaitFor([&] { return store.profile(id)->fideLooked; }, 5000));
    EXPECT_TRUE(store.profile(id)->fideError.isEmpty());
    EXPECT_EQ(store.profile(id)->fide->id, 1503014);
}
