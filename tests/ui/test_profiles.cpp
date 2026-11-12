// Profiles: accounts whose games are downloaded (from a fake Lichess and chess.com, never the
// real ones), over-the-board games added by hand, and the databases built from them.

#include "databases/DatabaseManager.hpp"
#include "explorer/ExplorerPanel.hpp"
#include "explorer/LichessExplorer.hpp"
#include "library/Library.hpp"
#include "profiles/Fide.hpp"
#include "profiles/LichessLogin.hpp"
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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTableView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QUrlQuery>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <map>
#include <memory>

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
            const auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                // A whole request: the headers, then as much body as Content-Length says.
                buffer->append(socket->readAll());
                const qsizetype end = buffer->indexOf("\r\n\r\n");
                if (end < 0) {
                    return;
                }
                qsizetype length = 0;
                for (const QByteArray& line : buffer->left(end).split('\n')) {
                    if (line.toLower().startsWith("content-length:")) {
                        length = line.mid(15).trimmed().toLongLong();
                    }
                }
                if (buffer->size() < end + 4 + length) {
                    return;
                }
                const QByteArray& request = *buffer;
                const QByteArray target = request.split(' ').value(1);
                asked << QString::fromUtf8(target);
                requests << request;
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
    QByteArrayList requests; ///< whole: method, headers, body

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
    kiebitz::app::LichessLogin lichess{kiebitz::app::LichessLogin::Storage{}};
    kiebitz::app::ProfilesView view(store, lichess, style);
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
    kiebitz::app::ExplorerPanel explorer(databases, store, repertoires, lichess, style);
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
        kiebitz::app::LichessLogin lichess{kiebitz::app::LichessLogin::Storage{}};
        kiebitz::app::ProfilesView view(store, lichess, style);
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

TEST_F(ProfilesTest, SigningInToLichessKeepsTheTokenAndWhoItIs) {
    using kiebitz::app::LichessLogin;
    server.pages["/api/token"] = R"({"token_type":"Bearer","access_token":"lio_secret"})";
    server.pages["/api/account"] = R"({"id":"alice","username":"Alice"})";
    std::optional<QByteArray> kept;
    LichessLogin::Storage storage;
    storage.loadToken = [&] {
        return kept;
    };
    storage.saveToken = [&](const QByteArray& token) {
        kept = token;
    };
    storage.removeToken = [&] {
        kept.reset();
    };
    QString name;
    storage.loadUsername = [&] {
        return name;
    };
    storage.saveUsername = [&](const QString& value) {
        name = value;
    };
    LichessLogin login(storage);
    login.setServer(server.url());
    // The "browser": Lichess sends it back to the address the sign-in page names.
    QUrl opened;
    QNetworkAccessManager browser;
    login.setOpenUrl([&](const QUrl& url) {
        opened = url;
        const QUrlQuery query(url);
        QUrl back(query.queryItemValue(QStringLiteral("redirect_uri")));
        back.setQuery(QStringLiteral("code=abc&state=") +
                      query.queryItemValue(QStringLiteral("state")));
        const QNetworkReply* reply = browser.get(QNetworkRequest(back));
        QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    });
    EXPECT_FALSE(login.signedIn());
    login.signIn();
    EXPECT_EQ(opened.path(), QStringLiteral("/oauth"));
    EXPECT_EQ(QUrlQuery(opened).queryItemValue(QStringLiteral("code_challenge_method")),
              QStringLiteral("S256"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return login.signedIn(); }, 5000));
    EXPECT_EQ(login.username(), QStringLiteral("Alice"));
    EXPECT_EQ(login.token(), QByteArray("lio_secret"));
    EXPECT_EQ(kept, QByteArray("lio_secret"));
    EXPECT_FALSE(login.busy());
    // The token request had the code and the verifier; the account request the token.
    const auto sent = [&](const char* path) {
        for (const QByteArray& request : server.requests) {
            if (request.split(' ').value(1).startsWith(path)) {
                return request;
            }
        }
        return QByteArray();
    };
    EXPECT_TRUE(sent("/api/token").startsWith("POST"));
    EXPECT_TRUE(sent("/api/token").contains("code=abc"));
    EXPECT_TRUE(sent("/api/token").contains("code_verifier="));
    EXPECT_TRUE(sent("/api/account").contains("Bearer lio_secret"));

    // Started again, it is still signed in; signed out, the token is gone here and on Lichess.
    LichessLogin again(storage);
    again.setServer(server.url());
    EXPECT_TRUE(again.signedIn());
    EXPECT_EQ(again.token(), QByteArray("lio_secret"));
    again.signOut();
    EXPECT_FALSE(again.signedIn());
    EXPECT_FALSE(kept.has_value());
    EXPECT_TRUE(QTest::qWaitFor(
        [&] {
            return std::any_of(
                server.requests.begin(), server.requests.end(),
                [](const QByteArray& r) { return r.startsWith("DELETE /api/token"); });
        },
        5000));
}

TEST_F(ProfilesTest, ALichessSignInYouTurnDownChangesNothing) {
    using kiebitz::app::LichessLogin;
    LichessLogin login(LichessLogin::Storage{});
    login.setServer(server.url());
    QNetworkAccessManager browser;
    login.setOpenUrl([&](const QUrl& url) {
        QUrl back(QUrlQuery(url).queryItemValue(QStringLiteral("redirect_uri")));
        back.setQuery(QStringLiteral("error=access_denied&state=x"));
        const QNetworkReply* reply = browser.get(QNetworkRequest(back));
        QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    });
    const QSignalSpy failed(&login, &LichessLogin::failed);
    login.signIn();
    EXPECT_TRUE(login.busy());
    ASSERT_TRUE(QTest::qWaitFor([&] { return !login.busy(); }, 5000));
    EXPECT_FALSE(login.signedIn());
    EXPECT_EQ(failed.count(), 0);
}

TEST(LichessExplorer, AsksForTheFilterLichessKnows) {
    using kiebitz::app::LichessExplorer;
    const kiebitz::core::Position position;
    const auto e4 = position.parseSan("e4").value();
    kiebitz::gamedb::ExploreFilter filter;
    filter.minRating = 1700;
    filter.maxRating = 2100;
    filter.fromYear = 2020;
    filter.speeds =
        static_cast<std::uint8_t>((1U << static_cast<unsigned>(kiebitz::gamedb::Speed::Blitz)) |
                                  (1U << static_cast<unsigned>(kiebitz::gamedb::Speed::Rapid)));
    const QUrl url = LichessExplorer::url(QUrl(QStringLiteral("https://x")),
                                          LichessExplorer::Kind::Lichess, {e4}, filter);
    const QUrlQuery query(url);
    EXPECT_EQ(url.path(), QStringLiteral("/lichess"));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("play")), QStringLiteral("e2e4"));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("speeds")), QStringLiteral("blitz,rapid"));
    // 1600–1800 holds 1700; up to 2000–2200, which holds 2100.
    EXPECT_EQ(query.queryItemValue(QStringLiteral("ratings")), QStringLiteral("1600,1800,2000"));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("since")), QStringLiteral("2020-01"));
    EXPECT_FALSE(query.hasQueryItem(QStringLiteral("until")));
    // The masters: only the years.
    const QUrlQuery masters(LichessExplorer::url(QUrl(QStringLiteral("https://x")),
                                                 LichessExplorer::Kind::Masters, {}, filter));
    EXPECT_EQ(masters.queryItemValue(QStringLiteral("since")), QStringLiteral("2020"));
    EXPECT_FALSE(masters.hasQueryItem(QStringLiteral("speeds")));
}

TEST_F(ProfilesTest, TheExplorerShowsLichessOnceSignedIn) {
    using kiebitz::app::LichessLogin;
    server.pages["/masters"] =
        R"({"white":60,"draws":30,"black":10,"moves":[)"
        R"({"uci":"e2e4","san":"e4","averageRating":2410,"white":40,"draws":20,"black":5},)"
        R"({"uci":"d2d4","san":"d4","averageRating":2430,"white":20,"draws":10,"black":5}],)"
        R"("topGames":[{"uci":"e2e4","id":"abcd1234","winner":"white",)"
        R"("white":{"name":"Carlsen, M.","rating":2863},"black":{"name":"Caruana, F.",)"
        R"("rating":2828},"year":2019,"month":"2019-04"}]})";
    server.pages["/masters/pgn/abcd1234"] =
        "[White \"Carlsen, M.\"]\n[Black \"Caruana, F.\"]\n[Result \"1-0\"]\n\n1. e4 e5 1-0\n";
    const kiebitz::app::AppStyle style(*qApp);
    ProfileStore store(folder.filePath(QStringLiteral("profiles")));
    kiebitz::app::DatabaseManager databases(folder.filePath(QStringLiteral("databases")));
    kiebitz::app::Library library;
    kiebitz::app::RepertoireStore repertoires(library);
    // Signed out: the explorer says what to do.
    std::optional<QByteArray> token;
    QString name;
    LichessLogin::Storage storage;
    storage.loadToken = [&] {
        return token;
    };
    storage.saveToken = [&](const QByteArray& value) {
        token = value;
    };
    storage.removeToken = [&] {
        token.reset();
    };
    storage.loadUsername = [&] {
        return name;
    };
    storage.saveUsername = [&](const QString& value) {
        name = value;
    };
    LichessLogin signedOut(storage);
    kiebitz::app::ExplorerPanel offline(databases, store, repertoires, signedOut, style);
    offline.show();
    auto* offlineSource = offline.findChild<QComboBox*>(QStringLiteral("explorerSource"));
    offlineSource->setCurrentIndex(offlineSource->findData(QStringLiteral("lichess:masters")));
    emit offlineSource->activated(offlineSource->currentIndex());
    EXPECT_TRUE(offline.findChild<QWidget*>(QStringLiteral("explorerSignIn"))->isVisible());

    // Signed in: the masters' moves and top games, asked for with the token.
    token = QByteArray("lio_secret");
    name = QStringLiteral("Alice");
    LichessLogin login(storage);
    kiebitz::app::ExplorerPanel explorer(databases, store, repertoires, login, style);
    explorer.lichessExplorer().setServers(server.url(), server.url());
    explorer.show();
    auto* source = explorer.findChild<QComboBox*>(QStringLiteral("explorerSource"));
    source->setCurrentIndex(source->findData(QStringLiteral("lichess:masters")));
    emit source->activated(source->currentIndex());
    ASSERT_TRUE(QTest::qWaitFor([&] { return explorer.moveTexts().size() == 2; }, 5000));
    EXPECT_EQ(explorer.moveTexts().front(), QStringLiteral("e4 65 2410"));
    EXPECT_FALSE(explorer.findChild<QWidget*>(QStringLiteral("explorerSignIn"))->isVisible());
    EXPECT_TRUE(std::any_of(server.requests.begin(), server.requests.end(), [](const auto& r) {
        return r.startsWith("GET /masters") && r.contains("Bearer lio_secret");
    }));
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        explorer.resize(460, 520);
        QTest::qWait(100);
        explorer.grab().save(QDir(dir).filePath(QStringLiteral("explorer-masters.png")));
    }
    // A top game opens whole.
    auto* games = explorer.findChild<QTreeWidget*>(QStringLiteral("explorerGames"));
    ASSERT_EQ(games->topLevelItemCount(), 1);
    EXPECT_EQ(games->topLevelItem(0)->text(0), QStringLiteral("Carlsen, M. (2863)"));
    QSignalSpy opened(&explorer, &kiebitz::app::ExplorerPanel::openGameRequested);
    emit games->itemDoubleClicked(games->topLevelItem(0), 0);
    EXPECT_TRUE(opened.wait(5000));
    QSettings().remove(QStringLiteral("explorer/source")); // other tests start without a choice
}
