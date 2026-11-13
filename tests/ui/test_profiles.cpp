// Profiles: accounts whose games are downloaded (from a fake Lichess and chess.com, never the
// real ones), over-the-board games added by hand, and the databases built from them.

#include "databases/DatabaseManager.hpp"
#include "explorer/ExplorerPanel.hpp"
#include "explorer/LichessExplorer.hpp"
#include "fake_server.hpp"
#include "library/Library.hpp"
#include "net/Lichess.hpp"
#include "profiles/Fide.hpp"
#include "profiles/LichessLogin.hpp"
#include "profiles/ProfileStore.hpp"
#include "profiles/ProfilesView.hpp"
#include "repertoire/RepertoireStore.hpp"
#include "sound/Sounds.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"
#include "widgets/Message.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTableView>
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

using kiebitz::test::FakeServer;

using kiebitz::app::AccountSite;
using kiebitz::app::ProfileStore;

QByteArray game(const char* white, const char* black, const char* moves) {
    return QByteArray("[Site \"https://lichess.org/x\"]\n[Date \"2025.06.01\"]\n[White \"") +
           white + "\"]\n[Black \"" + black + "\"]\n[Result \"1-0\"]\n\n" + moves + " 1-0\n\n";
}

/// A stand-in for the Keychain and the settings: tokens and usernames in memory, kept across
/// LichessLogin objects (as across starts of the app).
struct FakeKeychain {
    std::map<QString, QByteArray> tokens; ///< by username in lower case
    QStringList names;

    kiebitz::app::LichessLogin::Storage storage() {
        return {
            [this](const QString& user) -> std::optional<QByteArray> {
                const auto found = tokens.find(user.toLower());
                return found != tokens.end() ? std::optional(found->second) : std::nullopt;
            },
            [this](const QString& user, const QByteArray& token) {
                tokens[user.toLower()] = token;
            },
            [this](const QString& user) { tokens.erase(user.toLower()); },
            [this] { return names; },
            [this](const QStringList& value) { names = value; },
        };
    }
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
    EXPECT_TRUE(std::any_of(server.asked.begin(), server.asked.end(), [](const QString& asked) {
        return asked.startsWith(u"/api/games/user/alice") && asked.contains(u"clocks=true");
    })); // with their clocks
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

TEST_F(ProfilesTest, LichessGamesDownloadedWithoutClocksAreDownloadedOnceMore) {
    server.pages["/api/games/user/alice"] =
        game("alice", "bob", "1. e4 e5 2. Nf3") + game("carol", "alice", "1. d4 d5");
    server.pages["/api/user/alice"] = R"({"perfs":{}})";
    {
        ProfileStore store(folder.path());
        store.setServers(server.url(), server.url(), server.url());
        store.addAccount(store.profiles()[0].id, AccountSite::Lichess, QStringLiteral("alice"));
        waitFor(store, store.profiles()[0].id);
        store.wait();
    }
    // As an account from before clocks were asked for (it has no lichessClocks).
    QFile json(QDir(folder.path()).filePath(QStringLiteral("profiles.json")));
    ASSERT_TRUE(json.open(QIODevice::ReadOnly));
    QByteArray text = json.readAll();
    json.close();
    ASSERT_TRUE(text.contains("\"lichessClocks\": true"));
    text.replace("\"lichessClocks\": true", "\"lichessClocks\": false");
    ASSERT_TRUE(json.open(QIODevice::WriteOnly | QIODevice::Truncate));
    json.write(text);
    json.close();

    // The next sync downloads them all again (no "since"), and the copy replaces the old one:
    // the same two games, with a new one, not five.
    server.pages["/api/games/user/alice"] = game("alice", "bob", "1. e4 e5 2. Nf3") +
                                            game("carol", "alice", "1. d4 d5") +
                                            game("alice", "dave", "1. c4 c5");
    server.asked.clear();
    ProfileStore store(folder.path());
    store.setServers(server.url(), server.url(), server.url());
    const QString me = store.profiles()[0].id;
    store.sync(me);
    waitFor(store, me);
    EXPECT_TRUE(std::any_of(server.asked.begin(), server.asked.end(), [](const QString& asked) {
        return asked.startsWith(u"/api/games/user/alice") && !asked.contains(u"since=");
    }));
    EXPECT_EQ(store.profile(me)->all->gameCount(), 3U);
    EXPECT_TRUE(store.profile(me)->accounts[0].lichessClocks);
    // And after that, only what's new again.
    server.asked.clear();
    store.sync(me);
    waitFor(store, me);
    EXPECT_TRUE(std::any_of(server.asked.begin(), server.asked.end(), [](const QString& asked) {
        return asked.startsWith(u"/api/games/user/alice?since=");
    }));
}

TEST_F(ProfilesTest, AnAccountIsLookedUpBeforeItIsAdded) {
    server.pages["/api/user/catalansimp"] = R"({"id":"catalansimp","username":"CatalanSimp"})";
    server.pages["/api/user/gone"] = R"({"id":"gone","username":"Gone","disabled":true})";
    server.pages["/pub/player/bob"] = R"({"username":"bob","player_id":1})";
    ProfileStore store(folder.path());
    store.setServers(server.url(), server.url(), server.url());
    struct Answer {
        QString name;
        QString error;
        bool done = false;
    };
    const auto check = [&](AccountSite site, const char* name) {
        Answer answer;
        store.checkAccount(site, QString::fromUtf8(name),
                           [&answer](const QString& found, const QString& error) {
                               answer = {found, error, true};
                           });
        EXPECT_TRUE(QTest::qWaitFor([&] { return answer.done; }, 5000));
        return answer;
    };
    // There: under the site's own spelling.
    EXPECT_EQ(check(AccountSite::Lichess, "catalansimp").name, QStringLiteral("CatalanSimp"));
    EXPECT_EQ(check(AccountSite::ChessCom, "Bob").name, QStringLiteral("Bob"));
    // Not there (404), or closed: no name, no error.
    const Answer missing = check(AccountSite::Lichess, "lsadkjföd");
    EXPECT_TRUE(missing.name.isEmpty());
    EXPECT_TRUE(missing.error.isEmpty());
    EXPECT_TRUE(check(AccountSite::Lichess, "gone").name.isEmpty());
    // The site away: an error, so nothing is added.
    store.setServers(QUrl(QStringLiteral("http://127.0.0.1:1")), server.url(), server.url());
    EXPECT_FALSE(check(AccountSite::Lichess, "catalansimp").error.isEmpty());
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
    kiebitz::app::Library library;
    kiebitz::app::RepertoireStore repertoires(library);
    kiebitz::app::ProfilesView view(store, lichess, repertoires, style);
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

/// Two players as Lichess's search of FIDE's rating list answers them.
const char* const kFideAnswer =
    R"([{"id":99,"name":"Sanico, Magnus Carlsen","federation":"PHI","year":2001,)"
    R"("standard":1500,"gender":"M"},)"
    R"({"id":1503014,"name":"Carlsen, Magnus","federation":"NOR","year":1990,"title":"GM",)"
    R"("standard":2823,"rapid":2801,"blitz":2860,"gender":"M"}])";

} // namespace

TEST(Fide, TheLinkIsLichesssPageWithTheNameAsLichessWritesIt) {
    kiebitz::app::FidePlayer player;
    player.id = 1503014;
    player.name = QStringLiteral("Carlsen, Magnus");
    EXPECT_EQ(player.profileUrl(),
              QStringLiteral("https://lichess.org/fide/1503014/Carlsen_Magnus"));
    player.id = 556001355;
    player.name = QStringLiteral("Støren, Vetle  Andrè");
    EXPECT_EQ(player.profileUrl(),
              QStringLiteral("https://lichess.org/fide/556001355/Stren_Vetle_Andre"));
}

TEST(Fide, ReadsTheSearchAndRanksTheClosestNameFirst) {
    const auto players = kiebitz::app::parseFideSearch(kFideAnswer);
    ASSERT_EQ(players.size(), 2U);
    EXPECT_EQ(players[1].id, 1503014);
    EXPECT_EQ(players[1].name, QStringLiteral("Carlsen, Magnus"));
    EXPECT_EQ(players[1].title, QStringLiteral("GM"));
    EXPECT_EQ(players[1].federation, QStringLiteral("NOR"));
    EXPECT_EQ(players[1].standard, 2823);
    EXPECT_EQ(players[1].blitz, 2860);
    EXPECT_EQ(players[1].born, 1990);
    EXPECT_EQ(players[0].title, QString()); // no title, no rapid rating
    EXPECT_EQ(players[0].rapid, 0);
    EXPECT_TRUE(kiebitz::app::parseFideSearch("not json").empty());
    // "Magnus Carlsen" fits Carlsen, Magnus exactly; Sanico has a third name.
    const auto ranked = kiebitz::app::rankFideMatches(players, QStringLiteral("magnus CARLSEN"));
    ASSERT_EQ(ranked.size(), 2U);
    EXPECT_EQ(ranked[0].id, 1503014);
    EXPECT_EQ(kiebitz::app::rankFideMatches(players, QStringLiteral("Hikaru Nakamura")).size(), 0U);
}

TEST_F(ProfilesTest, AProfileFindsItsFidePlayerAndCanPickAnother) {
    server.pages["/api/fide/player"] = kFideAnswer;
    ProfileStore store(folder.path());
    store.setServers(server.url(), server.url(), server.url());
    const QString id = store.addProfile(QStringLiteral("Magnus Carlsen"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return store.profile(id)->fideLooked; }, 5000));
    EXPECT_TRUE(server.asked.back().startsWith(u"/api/fide/player?q=Magnus"));
    ASSERT_TRUE(store.profile(id)->fide.has_value());
    EXPECT_EQ(store.profile(id)->fide->id, 1503014);
    EXPECT_EQ(store.profile(id)->fideCandidates.size(), 2U);

    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        const kiebitz::app::AppStyle style(*qApp);
        kiebitz::app::LichessLogin lichess{kiebitz::app::LichessLogin::Storage{}};
        kiebitz::app::Library library;
        kiebitz::app::RepertoireStore repertoires(library);
        kiebitz::app::ProfilesView view(store, lichess, repertoires, style);
        view.resize(1180, 720);
        view.show();
        view.select(id);
        QTest::mouseClick(view.findChild<QPushButton*>(QStringLiteral("profileFide")),
                          Qt::LeftButton);
        QTest::qWait(150);
        for (QWidget* top : QApplication::topLevelWidgets()) {
            if (top->objectName() == u"fideCard" && top->isVisible()) {
                // Asked once: is this them? The other players stay hidden until "No".
                const auto* others = top->findChild<QWidget*>(QStringLiteral("fideOthers"));
                const auto* found = top->findChild<QWidget*>(QStringLiteral("fidePlayer"));
                EXPECT_FALSE(others->isVisible());
                EXPECT_NE(top->findChild<QPushButton*>(QStringLiteral("fideYes")), nullptr);
                top->grab().save(QDir(dir).filePath(QStringLiteral("fide-card.png")));
                top->findChild<QPushButton*>(QStringLiteral("fideNo"))->click();
                EXPECT_TRUE(others->isVisible());
                EXPECT_FALSE(found->isVisible()); // not them: gone
                QTest::qWait(100);
                top->grab().save(QDir(dir).filePath(QStringLiteral("fide-card-others.png")));
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
    EXPECT_FALSE(store.profile(id)->fideLooked);    // so it is tried again next time
    server.pages["/api/fide/player"] = kFideAnswer; // FIDE is back
    store.lookUpFide(id);
    ASSERT_TRUE(QTest::qWaitFor([&] { return store.profile(id)->fideLooked; }, 5000));
    EXPECT_TRUE(store.profile(id)->fideError.isEmpty());
    EXPECT_EQ(store.profile(id)->fide->id, 1503014);
}

TEST_F(ProfilesTest, SigningInToLichessKeepsTheTokenAndWhoItIs) {
    using kiebitz::app::LichessLogin;
    server.pages["/api/token"] = R"({"token_type":"Bearer","access_token":"lio_secret"})";
    server.pages["/api/account"] = R"({"id":"alice","username":"Alice"})";
    FakeKeychain keychain;
    const LichessLogin::Storage storage = keychain.storage();
    LichessLogin login(storage);
    login.setServer(server.url());
    // The "browser": Lichess sends it back to the address the sign-in page names.
    QUrl opened;
    QUrl back; // where Lichess sends the browser
    QNetworkAccessManager browser;
    QList<QUrl> landed; // where the browser ended up
    login.setOpenUrl([&](const QUrl& url) {
        opened = url;
        const QUrlQuery query(url);
        back = QUrl(query.queryItemValue(QStringLiteral("redirect_uri")));
        back.setQuery(QStringLiteral("code=abc&state=") +
                      query.queryItemValue(QStringLiteral("state")));
        QNetworkReply* reply = browser.get(QNetworkRequest(back));
        QObject::connect(reply, &QNetworkReply::finished, reply, [&landed, reply] {
            landed << reply->url();
            reply->deleteLater();
        });
    });
    EXPECT_FALSE(login.signedIn());
    const QSignalSpy front(&login, &LichessLogin::signedInNow);
    login.signIn();
    EXPECT_EQ(opened.path(), QStringLiteral("/oauth"));
    EXPECT_EQ(QUrlQuery(opened).queryItemValue(QStringLiteral("code_challenge_method")),
              QStringLiteral("S256"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return login.signedIn(); }, 5000));
    EXPECT_EQ(login.usernames(), QStringList{QStringLiteral("Alice")});
    EXPECT_TRUE(login.signedIn(QStringLiteral("alice"))); // any case
    EXPECT_EQ(front.count(), 1);                          // Kiebitz comes back to the front
    EXPECT_EQ(login.token(), QByteArray("lio_secret"));
    EXPECT_EQ(keychain.tokens[QStringLiteral("alice")], QByteArray("lio_secret"));
    EXPECT_FALSE(login.busy());
    // The browser went on to the account's page on Lichess, not a page from this Mac.
    ASSERT_TRUE(QTest::qWaitFor([&] { return !landed.isEmpty(); }, 5000));
    EXPECT_EQ(landed.front().path(), QStringLiteral("/@/Alice"));
    // A reload of the address it came back to: still answered (on to Lichess), not refused.
    QNetworkReply* reload = browser.get(QNetworkRequest(back));
    ASSERT_TRUE(QTest::qWaitFor([&] { return reload->isFinished(); }, 5000));
    EXPECT_NE(reload->error(), QNetworkReply::ConnectionRefusedError);
    EXPECT_EQ(reload->url().port(), server.url().port()); // sent on to "Lichess"
    reload->deleteLater();
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

    // A second account, signed in after the first, from its card: Lichess is asked for that
    // account. Both are kept, each with its token.
    server.pages["/api/token"] = R"({"token_type":"Bearer","access_token":"lio_other"})";
    server.pages["/api/account"] = R"({"id":"bob","username":"Bob"})";
    login.signIn(QStringLiteral("Bob"));
    EXPECT_EQ(QUrlQuery(opened).queryItemValue(QStringLiteral("username")), QStringLiteral("Bob"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return login.signedIn(QStringLiteral("Bob")); }, 5000));
    EXPECT_EQ(login.usernames(), (QStringList{QStringLiteral("Alice"), QStringLiteral("Bob")}));
    EXPECT_EQ(login.token(QStringLiteral("bob")), QByteArray("lio_other"));
    EXPECT_EQ(login.token(QStringLiteral("Alice")), QByteArray("lio_secret"));
    EXPECT_EQ(login.token(QStringLiteral("carol")), QByteArray()); // not signed in

    // Started again, both are still signed in; Alice signed out, her token is gone here and on
    // Lichess, Bob's stays.
    LichessLogin again(storage);
    again.setServer(server.url());
    EXPECT_EQ(again.usernames(), (QStringList{QStringLiteral("Alice"), QStringLiteral("Bob")}));
    EXPECT_EQ(again.token(QStringLiteral("Alice")), QByteArray("lio_secret"));
    again.signOut(QStringLiteral("alice"));
    EXPECT_EQ(again.usernames(), QStringList{QStringLiteral("Bob")});
    EXPECT_FALSE(keychain.tokens.contains(QStringLiteral("alice")));
    EXPECT_EQ(again.token(), QByteArray("lio_other")); // the explorer uses Bob's now
    EXPECT_TRUE(QTest::qWaitFor(
        [&] {
            return std::any_of(
                server.requests.begin(), server.requests.end(), [](const QByteArray& r) {
                    return r.startsWith("DELETE /api/token") && r.contains("Bearer lio_secret");
                });
        },
        5000));
}

TEST_F(ProfilesTest, LichessLoginsAreKeptInAFileOnlyYouCanRead) {
    using kiebitz::app::LichessLogin;
    const QString path = folder.filePath(QStringLiteral("support/lichess-logins.json"));
    const LichessLogin::Storage storage = LichessLogin::privateFile(path);
    EXPECT_TRUE(storage.loadUsernames().isEmpty());
    storage.saveToken(QStringLiteral("CatalanSimp"), "lio_one");
    storage.saveToken(QStringLiteral("ConstantlyTilting"), "lio_two");
    storage.saveToken(QStringLiteral("catalansimp"), "lio_new"); // the same account, any case
    EXPECT_EQ(storage.loadUsernames(),
              (QStringList{QStringLiteral("ConstantlyTilting"), QStringLiteral("catalansimp")}));
    EXPECT_EQ(storage.loadToken(QStringLiteral("CATALANSIMP")), QByteArray("lio_new"));
    EXPECT_EQ(storage.loadToken(QStringLiteral("ConstantlyTilting")), QByteArray("lio_two"));
    EXPECT_FALSE(storage.loadToken(QStringLiteral("nobody")).has_value());
    // Only you can read it.
    EXPECT_EQ(QFile::permissions(path) & ~(QFileDevice::ReadUser | QFileDevice::WriteUser),
              QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    // Signed out: gone from the file; the other stays.
    storage.removeToken(QStringLiteral("ConstantlyTilting"));
    EXPECT_EQ(storage.loadUsernames(), QStringList{QStringLiteral("catalansimp")});
    // Read by a login as it starts: signed in, with the token.
    LichessLogin login(storage);
    EXPECT_TRUE(login.signedIn(QStringLiteral("CatalanSimp")));
    EXPECT_EQ(login.token(QStringLiteral("CatalanSimp")), QByteArray("lio_new"));
}

TEST_F(ProfilesTest, ALichessSignInYouTurnDownChangesNothing) {
    using kiebitz::app::LichessLogin;
    LichessLogin login(LichessLogin::Storage{});
    login.setServer(server.url());
    QNetworkAccessManager browser;
    login.setOpenUrl([&](const QUrl& url) {
        const QUrlQuery query(url);
        QUrl back(query.queryItemValue(QStringLiteral("redirect_uri")));
        back.setQuery(QStringLiteral("error=access_denied&state=") +
                      query.queryItemValue(QStringLiteral("state")));
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
    FakeKeychain keychain;
    const LichessLogin::Storage storage = keychain.storage();
    LichessLogin signedOut(storage);
    kiebitz::app::ExplorerPanel offline(databases, store, repertoires, signedOut, style);
    offline.show();
    auto* offlineSource = offline.findChild<QComboBox*>(QStringLiteral("explorerSource"));
    offlineSource->setCurrentIndex(offlineSource->findData(QStringLiteral("lichess:masters")));
    emit offlineSource->activated(offlineSource->currentIndex());
    EXPECT_TRUE(offline.findChild<QWidget*>(QStringLiteral("explorerSignIn"))->isVisible());

    // Signed in: the masters' moves and top games, asked for with the token.
    keychain.tokens[QStringLiteral("alice")] = QByteArray("lio_secret");
    keychain.names = QStringList{QStringLiteral("Alice")};
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

TEST_F(ProfilesTest, AfterTooManyRequestsLichessIsLeftAloneForAMinute) {
    using kiebitz::app::LichessExplorer;
    namespace lichess = kiebitz::app::lichess;
    lichess::endPause();
    // A download Lichess answers with 429: it says to wait, and the minute's pause starts.
    server.statuses["/api/games/user/alice"] = 429;
    server.pages["/api/user/alice"] = R"({"perfs":{}})";
    ProfileStore store(folder.path());
    store.setServers(server.url(), server.url(), server.url());
    const QString me = store.profiles()[0].id;
    QSignalSpy failed(&store, &ProfileStore::failed);
    store.addAccount(me, AccountSite::Lichess, QStringLiteral("alice"));
    waitFor(store, me);
    ASSERT_GE(failed.count(), 1);
    EXPECT_TRUE(failed.front().at(1).toString().contains(u"wait a minute"));
    EXPECT_GT(lichess::pauseLeftMs(), 50'000);
    // Kiebitz says who it is, and nothing about the user.
    EXPECT_TRUE(std::any_of(server.requests.begin(), server.requests.end(),
                            [](const auto& r) { return r.contains("User-Agent: Kiebitz/"); }));

    // Meanwhile the explorer doesn't ask Lichess at all: it says to wait.
    FakeKeychain keychain;
    kiebitz::app::LichessLogin login(keychain.storage());
    LichessExplorer explorer(login);
    explorer.setServers(server.url(), server.url());
    const qsizetype asked = server.asked.size();
    std::optional<LichessExplorer::Answer> answer;
    explorer.ask(LichessExplorer::Kind::Masters, {}, {},
                 [&](const LichessExplorer::Answer& a) { answer = a; });
    ASSERT_TRUE(QTest::qWaitFor([&] { return answer.has_value(); }, 5000));
    EXPECT_TRUE(answer->error.contains(u"slow down"));
    EXPECT_EQ(server.asked.size(), asked);

    // And a second download waits too, without asking.
    store.sync(me);
    waitFor(store, me);
    EXPECT_EQ(server.asked.size(), asked);
    lichess::endPause(); // the other tests in this process
}

TEST(LiveNetwork, LichessKnowsFidePlayersByName) {
    if (qEnvironmentVariableIsEmpty("KIEBITZ_LIVE_NETWORK")) {
        GTEST_SKIP() << "set KIEBITZ_LIVE_NETWORK=1 to run";
    }
    const QTemporaryDir folder;
    ProfileStore store(folder.path()); // the real servers
    const QString id = store.addProfile(QStringLiteral("Magnus Carlsen"));
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return store.profile(id)->fideLooked || !store.profile(id)->fideError.isEmpty(); },
        15000));
    EXPECT_TRUE(store.profile(id)->fideError.isEmpty())
        << store.profile(id)->fideError.toStdString();
    ASSERT_TRUE(store.profile(id)->fide.has_value());
    EXPECT_EQ(store.profile(id)->fide->id, 1503014);
    EXPECT_EQ(store.profile(id)->fide->federation, QStringLiteral("NOR"));
    EXPECT_GT(store.profile(id)->fide->standard, 2700);
}

TEST_F(ProfilesTest, TheRepertoireTabShowsWhereGamesLeftTheBook) {
    // A White repertoire: 1. e4 e5 2. Nf3 Nc6 3. Bb5; against 1... c5, 2. Nf3.
    const QString books = folder.filePath(QStringLiteral("books"));
    QDir().mkpath(books);
    QFile book(QDir(books).filePath(QStringLiteral("white.pgn")));
    ASSERT_TRUE(book.open(QIODevice::WriteOnly));
    book.write("[FileType \"Repertoire\"]\n[Orientation \"white\"]\n\n"
               "1. e4 e5 (1... c5 2. Nf3) 2. Nf3 Nc6 3. Bb5 *\n");
    book.close();
    kiebitz::app::Library library;
    kiebitz::app::RepertoireStore repertoires(library); // reads the files once they're found
    QSignalSpy scanned(&library, &kiebitz::app::Library::scanned);
    library.setFolder(books);
    ASSERT_TRUE(scanned.wait(5000));
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return repertoires.get(kiebitz::core::Color::White) != nullptr; }, 5000));

    // Alice's over-the-board games: 2. Bc4 twice (she left the book), 1... e6 once (they did),
    // and one with Black (no Black repertoire: not checked).
    ProfileStore store(folder.filePath(QStringLiteral("profiles")));
    store.setServers(server.url(), server.url(), server.url());
    const QString id = store.addProfile(QStringLiteral("Alice Smith"));
    QFile pgn(folder.filePath(QStringLiteral("club.pgn")));
    ASSERT_TRUE(pgn.open(QIODevice::WriteOnly));
    pgn.write(game("Smith, Alice", "Bob", "1. e4 e5 2. Bc4 Nf6"));
    pgn.write(game("Smith, Alice", "Carol", "1. e4 e5 2. Bc4 Bc5"));
    pgn.write(game("Smith, Alice", "Dave", "1. e4 e6 2. d4"));
    pgn.write(game("Eve", "Smith, Alice", "1. d4 d5"));
    pgn.write(game("Smith, Alice", "Frank", "1. c4 e5")); // she left it on her first move
    pgn.close();
    store.addOtbGames(id, {pgn.fileName()});
    waitFor(store, id);

    const kiebitz::app::AppStyle style(*qApp);
    kiebitz::app::LichessLogin lichess{kiebitz::app::LichessLogin::Storage{}};
    kiebitz::app::ProfilesView view(store, lichess, repertoires, style);
    view.resize(1180, 720);
    view.show();
    view.select(id);
    QTest::mouseClick(view.findChild<QPushButton*>(QStringLiteral("profileShowRepertoire")),
                      Qt::LeftButton);
    // Done (the profile may still be refreshing, which checks again).
    ASSERT_TRUE(QTest::qWaitFor(
        [&] {
            return view.theorySummary().contains(u"games checked") && store.status(id).isEmpty();
        },
        5000))
        << view.theorySummary().toStdString();
    QTest::qWait(200);
    ASSERT_TRUE(
        QTest::qWaitFor([&] { return view.theorySummary().contains(u"games checked"); }, 5000));
    // Under their openings (the names come in the background).
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return !view.theoryRows().join(u'|').contains(u"Other openings"); }, 20000));
    QStringList rows = view.theoryRows();
    rows.sort(); // openings with as many games come in any order
    EXPECT_EQ(rows, (QStringList{QStringLiteral("Bishop's Opening: 2. Bc4  You  2  2. Nf3"),
                                 QStringLiteral("English Opening: 1. c4  You  1  1. e4"),
                                 QStringLiteral("French Defense: 1… e6  They  1  covers 1… e5, "
                                                "1… c5")}));
    // The line to the move, as a score sheet writes it.
    const auto* theory = view.findChild<QTreeWidget*>(QStringLiteral("profileTheory"));
    for (int i = 0; i < theory->topLevelItemCount(); ++i) {
        if (theory->topLevelItem(i)->text(0) == u"Bishop's Opening") {
            EXPECT_EQ(theory->topLevelItem(i)->child(0)->toolTip(0), QStringLiteral("1. e4 e5"));
        }
    }
    EXPECT_TRUE(view.theorySummary().startsWith(u"4 games checked")) // the Black one isn't
        << view.theorySummary().toStdString();
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        QTest::qWait(100);
        view.grab().save(QDir(dir).filePath(QStringLiteral("profile-repertoire.png")));
    }
    // Only where they left it.
    QTest::mouseClick(view.findChild<QPushButton*>(QStringLiteral("profileTheoryKind:opponent")),
                      Qt::LeftButton);
    EXPECT_EQ(view.theoryRows().size(), 1);
    // A game of the group, double-clicked: it opens at 2... (after the move that left the book).
    QTest::mouseClick(view.findChild<QPushButton*>(QStringLiteral("profileTheoryKind:all")),
                      Qt::LeftButton);
    auto* tree = view.findChild<QTreeWidget*>(QStringLiteral("profileTheory"));
    ASSERT_NE(tree, nullptr);
    ASSERT_TRUE(QTest::qWaitFor([&] { return tree->topLevelItemCount() == 3; }, 5000));
    QTreeWidgetItem* bc4 = tree->topLevelItem(0)->child(0);
    ASSERT_EQ(bc4->childCount(), 2);
    QSignalSpy opened(&view, &kiebitz::app::ProfilesView::openGameAtRequested);
    emit tree->itemDoubleClicked(bc4->child(0), 0);
    ASSERT_TRUE(opened.count() == 1 || opened.wait(5000)); // found in its PGN first
    EXPECT_EQ(opened.front().at(1).toInt(), 3);            // 1. e4 e5 2. Bc4
    // The move itself: the repertoire, there.
    const QSignalSpy book2(&view, &kiebitz::app::ProfilesView::repertoireRequested);
    emit tree->itemDoubleClicked(bc4, 0);
    EXPECT_EQ(book2.count(), 1);

    // Saved: another view (as after a restart) shows it at once, without checking again.
    kiebitz::app::ProfilesView second(store, lichess, repertoires, style);
    second.resize(1180, 720);
    second.show();
    second.select(id);
    const QSignalSpy shown(&second, &kiebitz::app::ProfilesView::theoryChecked);
    QTest::mouseClick(second.findChild<QPushButton*>(QStringLiteral("profileShowRepertoire")),
                      Qt::LeftButton);
    EXPECT_EQ(shown.count(), 1); // there and then, not after a scan
    EXPECT_TRUE(second.theorySummary().startsWith(u"4 games checked"));
    EXPECT_FALSE(second.theorySummary().contains(u"Updating"));
    // A new game: checked again (the old result shows meanwhile).
    QFile more(folder.filePath(QStringLiteral("more.pgn")));
    ASSERT_TRUE(more.open(QIODevice::WriteOnly));
    more.write(game("Smith, Alice", "Gina", "1. e4 e5 2. Bc4 Bc5"));
    more.close();
    store.addOtbGames(id, {more.fileName()});
    waitFor(store, id);
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return second.theorySummary().startsWith(u"5 games checked"); }, 5000))
        << second.theorySummary().toStdString();
}

TEST_F(ProfilesTest, OverTheBoardGamesAreAddedOnceAndCanBeDeleted) {
    ProfileStore store(folder.filePath(QStringLiteral("profiles")));
    store.setServers(server.url(), server.url(), server.url());
    const QString id = store.addProfile(QStringLiteral("Alice Smith"));
    QSignalSpy added(&store, &ProfileStore::otbAdded);
    // A file with a game twice, and another game.
    QFile pgn(folder.filePath(QStringLiteral("club.pgn")));
    ASSERT_TRUE(pgn.open(QIODevice::WriteOnly));
    pgn.write(game("Smith, Alice", "Bob", "1. e4 e5 2. Bc4 Nf6"));
    pgn.write(game("Smith, Alice", "Bob", "1. e4 { a comment } e5 2. Bc4 Nf6")); // the same game
    pgn.write(game("Carol", "Smith, Alice", "1. d4 d5"));
    pgn.close();
    store.addOtbGames(id, {pgn.fileName()});
    waitFor(store, id);
    ASSERT_EQ(added.size(), 1);
    EXPECT_EQ(added.front().at(1).toInt(), 2); // added
    EXPECT_EQ(added.front().at(2).toInt(), 1); // left out
    EXPECT_EQ(store.profile(id)->all->gameCount(), 2U);
    // The same file again: nothing new.
    store.addOtbGames(id, {pgn.fileName()});
    waitFor(store, id);
    ASSERT_EQ(added.size(), 2);
    EXPECT_EQ(added.back().at(1).toInt(), 0);
    EXPECT_EQ(added.back().at(2).toInt(), 3);
    EXPECT_EQ(store.profile(id)->all->gameCount(), 2U);

    // A duplicate already in the profile's files (from before this check): deleting one copy
    // keeps the other.
    const QString otb = folder.filePath(QStringLiteral("profiles/") + id + QStringLiteral("/otb"));
    QFile copy(QDir(otb).filePath(QStringLiteral("old copy.pgn")));
    ASSERT_TRUE(copy.open(QIODevice::WriteOnly));
    copy.write(game("Carol", "Smith, Alice", "1. d4 d5"));
    copy.close();
    QFile third(folder.filePath(QStringLiteral("third.pgn")));
    ASSERT_TRUE(third.open(QIODevice::WriteOnly));
    third.write(game("Smith, Alice", "Dave", "1. c4 c5"));
    third.close();
    store.addOtbGames(id, {third.fileName()}); // rebuilds with the old copy in it
    waitFor(store, id);
    ASSERT_EQ(store.profile(id)->all->gameCount(), 4U);
    // Carol's game, from the database: deleted once, there once more.
    const auto db = store.profile(id)->all;
    std::optional<kiebitz::core::Game> carol;
    for (const auto& info : db->games(1, 10)) {
        if (info.white == "Carol") {
            carol = db->game(info.id);
        }
    }
    ASSERT_TRUE(carol.has_value());
    store.deleteGame(id, QString(), *carol);
    waitFor(store, id);
    EXPECT_EQ(store.profile(id)->all->gameCount(), 3U);
    store.deleteGame(id, QStringLiteral("otb"), *carol);
    waitFor(store, id);
    EXPECT_EQ(store.profile(id)->all->gameCount(), 2U);
    // Opened, a game is as its PGN has it: with its clocks and comments.
    QFile timed(folder.filePath(QStringLiteral("timed.pgn")));
    ASSERT_TRUE(timed.open(QIODevice::WriteOnly));
    timed.write("[White \"Smith, Alice\"]\n[Black \"Erin\"]\n[Date \"2026.01.01\"]\n"
                "[Result \"1-0\"]\n\n1. e4 { [%clk 1:29:58] } e5 { [%clk 1:29:50] } 1-0\n");
    timed.close();
    store.addOtbGames(id, {timed.fileName()});
    waitFor(store, id);
    std::optional<kiebitz::core::Game> fromDb;
    for (const auto& info : store.profile(id)->all->games(1, 10)) {
        if (info.black == "Erin") {
            fromDb = store.profile(id)->all->game(info.id);
        }
    }
    ASSERT_TRUE(fromDb.has_value());
    const auto firstComment = [](const kiebitz::core::Game& g) {
        return g.node(g.mainLine().at(1)).comment;
    };
    EXPECT_EQ(firstComment(*fromDb), ""); // the database keeps only the moves
    std::optional<kiebitz::core::Game> original;
    store.originalGame(id, QString(), *fromDb,
                       [&](const kiebitz::core::Game& game) { original = game; });
    ASSERT_TRUE(QTest::qWaitFor([&] { return original.has_value(); }, 5000));
    EXPECT_EQ(firstComment(*original), "[%clk 1:29:58]");

    // Gone for good: deleting it again says so.
    QSignalSpy failed(&store, &ProfileStore::failed);
    store.deleteGame(id, QString(), *carol);
    ASSERT_TRUE(failed.wait(5000));
    EXPECT_EQ(store.profile(id)->all->gameCount(), 3U); // Erin's game was added since
}

TEST(MessageDialog, AsksWithButtonsNamedForWhatTheyDo) {
    using kiebitz::app::MessageDialog;
    const kiebitz::app::AppStyle style(*qApp);
    MessageDialog question(QStringLiteral("Remove Account"),
                           QStringLiteral("Remove lsadkjföd and its games from this profile?"), {},
                           QStringLiteral("Remove"), MessageDialog::Action::Destructive, nullptr);
    auto* act = question.findChild<QPushButton*>(QStringLiteral("messageAction"));
    auto* cancel = question.findChild<QPushButton*>(QStringLiteral("messageCancel"));
    ASSERT_NE(act, nullptr);
    ASSERT_NE(cancel, nullptr);
    EXPECT_EQ(act->text(), QStringLiteral("Remove"));
    EXPECT_EQ(act->property("role").toString(), QStringLiteral("danger"));
    EXPECT_TRUE(cancel->isDefault()); // Return doesn't remove anything
    MessageDialog note(QStringLiteral("Add Account"), QStringLiteral("There's no account."), {}, {},
                       MessageDialog::Action::Normal, nullptr);
    EXPECT_NE(note.findChild<QPushButton*>(QStringLiteral("messageOk")), nullptr);
    EXPECT_EQ(note.findChild<QPushButton*>(QStringLiteral("messageAction")), nullptr);
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        question.show();
        QTest::qWait(100);
        question.grab().save(QDir(dir).filePath(QStringLiteral("message.png")));
    }
}

TEST(Sounds, EverySetHasShortWavFilesAtTheVolumeChosen) {
    using kiebitz::app::Sounds;
    // The samples of a WAV the app wrote: after its header (format chunk of 16 bytes).
    const auto loudest = [](const QByteArray& wav) {
        int peak = 0;
        for (qsizetype i = 44; i + 1 < wav.size(); i += 2) {
            const auto sample = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(static_cast<std::uint8_t>(wav[i])) |
                static_cast<std::uint16_t>(static_cast<std::uint8_t>(wav[i + 1]) << 8U));
            peak = std::max(peak, std::abs(static_cast<int>(sample)));
        }
        return peak;
    };
    ASSERT_EQ(Sounds::sets().front(), QStringLiteral("standard")); // the default
    for (const QString& set : Sounds::sets()) {
        for (const auto kind : {Sounds::Kind::Move, Sounds::Kind::Capture, Sounds::Kind::Check}) {
            const QByteArray wav = Sounds::wav(set, kind, 70);
            ASSERT_GT(wav.size(), 44) << set.toStdString();
            EXPECT_TRUE(wav.startsWith("RIFF"));
            EXPECT_EQ(wav.mid(8, 8), QByteArray("WAVEfmt "));
            EXPECT_EQ(wav.mid(36, 4), QByteArray("data"));
            EXPECT_LT(wav.size() - 44, 44100 * 2); // under a second (16-bit mono)
            // Softer at a lower volume, silent at none.
            EXPECT_GT(loudest(Sounds::wav(set, kind, 100)), loudest(Sounds::wav(set, kind, 40)));
            EXPECT_EQ(loudest(Sounds::wav(set, kind, 0)), 0);
        }
    }
    EXPECT_TRUE(Sounds::wav(QStringLiteral("none"), Sounds::Kind::Move, 70).isEmpty());
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        QFile file(QDir(dir).filePath(QStringLiteral("capture.wav"))); // to check with afinfo
        if (file.open(QIODevice::WriteOnly)) {
            file.write(Sounds::wav(QStringLiteral("sfx"), Sounds::Kind::Capture, 70));
        }
    }
}

TEST(Icons, SidebarIconsSnapshot) {
    const kiebitz::app::AppStyle style(*qApp);
    const auto& c = style.colors();
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR"); !dir.isEmpty()) {
        // The sidebar's icons side by side, large, to see them as a set.
        QPixmap sheet(4 * 96, 96);
        sheet.fill(c.window);
        QPainter painter(&sheet);
        int x = 8;
        for (const auto kind : {kiebitz::app::IconKind::Person, kiebitz::app::IconKind::Engine,
                                kiebitz::app::IconKind::Gear, kiebitz::app::IconKind::Database}) {
            painter.drawPixmap(x, 8,
                               kiebitz::app::makeIcon(kind, c.text, c.dimmed, 80).pixmap(80, 80));
            x += 96;
        }
        painter.end();
        sheet.save(QDir(dir).filePath(QStringLiteral("icons.png")));
    }
    SUCCEED();
}
