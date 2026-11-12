#include "kiebitz/gamedb/database.hpp"
#include "kiebitz/gamedb/import.hpp"
#include "kiebitz/gamedb/scan.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace kiebitz;
using gamedb::GameResult;

class TempFolder {
public:
    TempFolder() {
        std::random_device random;
        m_path = std::filesystem::temp_directory_path() /
                 ("kiebitz-gamedb-test-" + std::to_string(random()));
        std::filesystem::create_directories(m_path);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
    TempFolder(TempFolder&&) = delete;
    TempFolder& operator=(TempFolder&&) = delete;
    ~TempFolder() { std::filesystem::remove_all(m_path); }
    [[nodiscard]] std::string path() const { return m_path.string(); }

private:
    std::filesystem::path m_path;
};

std::vector<core::Move> line(std::initializer_list<std::string_view> sans) {
    std::vector<core::Move> moves;
    core::Position position;
    for (const auto san : sans) {
        moves.push_back(position.parseSan(san).value());
        position.play(moves.back());
    }
    return moves;
}

const char* const kGames = R"([Event "Club"]
[White "Carlsen, Magnus"]
[Black "Caruana, Fabiano"]
[Result "1-0"]
[WhiteElo "2830"]
[BlackElo "2800"]
[Date "2024.01.02"]

1.e4 e5 {A comment} 2. Nf3 (2. Bc4 Nf6 (2... Bc5)) Nc6 $1 3. Bb5 a6 1-0

[Event "Open"]
[White "Smith"]
[Black "Carlsen, Magnus"]
[Result "0-1"]
[WhiteElo "2100"]
[BlackElo "2850"]

1. e4 c5 2. Nf3 d6 0-1

[Event "Open"]
[White "Jones"]
[Black "Brown"]
[Result "1/2-1/2"]

1. d4 d5 2. c4 e6 3. Nc3 Nf6 4. Bg5 Be7 5. e3 O-O 6. Nf3 h6 1/2-1/2

[Event "Blitz"]
[White "A"]
[Black "B"]
[Result "*"]

1. e4 e5 2. Nf3 Nf6 *
)";

} // namespace

TEST(GameScan, HeadersAndTheMainLine) {
    const auto game = gamedb::scanGame(
        "[White \"Carlsen, Magnus\"]\n[Black \"?\"]\n[WhiteElo \"2830\"]\n[Result \"1-0\"]\n"
        "[Date \"????.??.??\"]\n\n1.e4 e5 {comment (with parens)} 2. Nf3 (2. Bc4 {x} Nf6 (2... "
        "Bc5)) Nc6 $1 3. Bb5!? a6 4. O-O 1-0\n");
    EXPECT_EQ(game.white, "Carlsen, Magnus");
    EXPECT_EQ(game.black, ""); // "?" is unknown
    EXPECT_EQ(game.date, "");
    EXPECT_EQ(game.whiteElo, 2830);
    EXPECT_EQ(game.result, GameResult::WhiteWins);
    EXPECT_EQ(game.moves.size(), 7U); // e4 e5 Nf3 Nc6 Bb5 a6 O-O
    EXPECT_FALSE(game.damaged);
}

TEST(GameScan, ZeroCastlingBrokenMovesAndVariants) {
    EXPECT_EQ(gamedb::scanGame("1. e4 e5 2. Nf3 Nc6 3. Bc4 Bc5 4. 0-0 *").moves.size(), 7U);
    const auto broken = gamedb::scanGame("1. e4 e5 2. Ke4 Nc6 *");
    EXPECT_TRUE(broken.damaged);
    EXPECT_EQ(broken.moves.size(), 2U); // kept up to the broken move
    EXPECT_TRUE(gamedb::scanGame("[Variant \"Chess960\"]\n\n1. e4 *").skipped);
    const auto setUp = gamedb::scanGame("[FEN \"4k3/8/8/8/8/8/4P3/4K3 w - - 0 1\"]\n\n1. e4 *");
    EXPECT_FALSE(setUp.fen.empty());
    EXPECT_EQ(setUp.moves.size(), 1U);
}

TEST(GameSplitter, GamesAcrossPiecesOfAnySize) {
    for (const std::size_t piece : {std::size_t{1}, std::size_t{7}, std::size_t{4096}}) {
        gamedb::GameSplitter splitter;
        std::vector<std::string> games;
        const auto sink = [&](std::string_view game) {
            games.emplace_back(game);
        };
        const std::string text = std::string("\xEF\xBB\xBF") + kGames; // with a byte-order mark
        for (std::size_t at = 0; at < text.size(); at += piece) {
            splitter.feed(std::string_view(text).substr(at, piece), sink);
        }
        splitter.finish(sink);
        ASSERT_EQ(games.size(), 4U) << "pieces of " << piece;
        EXPECT_EQ(gamedb::scanGame(games[1]).white, "Smith");
        EXPECT_EQ(gamedb::scanGame(games[0]).white, "Carlsen, Magnus");
    }
}

TEST(GameDatabase, ImportedGamesCanBeExploredSearchedAndRead) {
    const TempFolder folder;
    {
        auto writer = gamedb::DatabaseWriter::create(folder.path(), {"Test", "test.pgn", 0, 0});
        ASSERT_TRUE(writer.ok()) << writer.error().message;
        const std::string text = kGames;
        std::size_t at = 0;
        gamedb::ImportOptions options;
        options.threads = 3;
        const auto result = gamedb::importPgn(
            **writer,
            [&](char* buffer, std::size_t size) {
                const auto n = std::min<std::size_t>({size, 13, text.size() - at});
                std::copy_n(text.data() + at, n, buffer);
                at += n;
                return n;
            },
            options);
        EXPECT_EQ(result.counts.games, 4U);
        EXPECT_EQ(result.counts.bytes, text.size());
        EXPECT_EQ((*writer)->finish(), "");
    }
    auto opened = gamedb::Database::open(folder.path());
    ASSERT_TRUE(opened.ok()) << opened.error().message;
    const gamedb::Database& db = **opened;
    EXPECT_EQ(db.gameCount(), 4U);
    EXPECT_EQ(db.info().name, "Test");
    EXPECT_EQ(db.info().players, 7U); // Carlsen once, Caruana, Smith, Jones, Brown, A, B

    // In the order of the file.
    const auto all = db.games(1, 10);
    ASSERT_EQ(all.size(), 4U);
    EXPECT_EQ(all[1].white, "Smith");
    EXPECT_EQ(all[2].result, GameResult::Draw);
    EXPECT_EQ(all[2].plies, 12);

    // The explorer: 1. e4 in three games, 1. d4 in one.
    const auto root = db.explore({});
    EXPECT_EQ(root.games, 4U);
    EXPECT_EQ(root.white, 1U);
    EXPECT_EQ(root.draws, 1U);
    EXPECT_EQ(root.black, 1U);
    ASSERT_EQ(root.moves.size(), 2U);
    EXPECT_EQ(root.moves[0].san, "e4");
    EXPECT_EQ(root.moves[0].games, 3U);
    EXPECT_EQ(root.moves[1].san, "d4");
    const auto e4e5 = db.explore(line({"e4", "e5", "Nf3"}));
    EXPECT_EQ(e4e5.games, 2U);
    ASSERT_EQ(e4e5.moves.size(), 2U); // Nc6 and Nf6
    EXPECT_TRUE(db.explore(line({"a4"})).moves.empty());
    // Average ratings by side: Carlsen–Caruana (2830/2800) and Smith–Carlsen (2100/2850).
    EXPECT_TRUE(db.hasRatings());
    EXPECT_EQ(root.whiteElo, 2465);
    EXPECT_EQ(root.blackElo, 2825);
    EXPECT_EQ(root.moves[0].whiteElo, 2465); // 1. e4: the same two rated games
    EXPECT_EQ(root.moves[1].whiteElo, 0);    // 1. d4: no ratings

    // The games of a line, best rated first.
    const auto best = db.gamesAt(line({"e4"}), 2);
    ASSERT_EQ(best.size(), 2U);
    EXPECT_EQ(best[0].white, "Carlsen, Magnus");
    EXPECT_EQ(best[1].white, "Smith");

    // Search: players (any case), events.
    EXPECT_EQ(db.search("carlsen", 10).size(), 2U);
    EXPECT_EQ(db.search("Blitz", 10).size(), 1U);

    // A whole game back, with its headers.
    const auto game = db.game(3);
    ASSERT_TRUE(game.has_value());
    EXPECT_EQ(game->header("White"), "Jones");
    EXPECT_EQ(game->mainLine().size(), 13U); // the root and 12 moves
}

TEST(GameDatabase, RatingsAreAddedToOlderDatabases) {
    const TempFolder folder;
    {
        auto writer = gamedb::DatabaseWriter::create(folder.path(), {"Old", "", 0, 0});
        ASSERT_TRUE(writer.ok());
        gamedb::GameSplitter splitter;
        const auto add = [&](std::string_view game) {
            (*writer)->add(gamedb::scanGame(game));
        };
        splitter.feed(kGames, add);
        splitter.finish(add);
        EXPECT_EQ((*writer)->finish(), "");
    }
    // As made before ratings.bin existed.
    std::filesystem::remove(std::filesystem::path(folder.path()) / "ratings.bin");
    std::filesystem::remove(std::filesystem::path(folder.path()) / "attrs.bin");
    {
        auto db = gamedb::Database::open(folder.path());
        ASSERT_TRUE(db.ok());
        EXPECT_FALSE((*db)->hasRatings());
        EXPECT_EQ((*db)->explore({}).whiteElo, 0);
        EXPECT_EQ((*db)->explore({}).games, 4U); // everything else works
    }
    EXPECT_EQ(gamedb::upgrade(folder.path()), "");
    auto db = gamedb::Database::open(folder.path());
    ASSERT_TRUE(db.ok());
    EXPECT_TRUE((*db)->hasRatings());
    EXPECT_EQ((*db)->explore({}).whiteElo, 2465);
    EXPECT_EQ((*db)->explore({}).blackElo, 2825);
}

TEST(GameDatabase, OnlineDatabasesKeepHigherRatedHumans) {
    // A 3100 blitz rating online is a human; over the board it would be an engine.
    const std::string games = "[Site \"https://lichess.org/abc\"]\n[White \"Fast\"]\n"
                              "[WhiteElo \"3100\"]\n[BlackElo \"3100\"]\n[Result \"1-0\"]\n\n"
                              "1. e4 e5 1-0\n\n"
                              "[Site \"Oslo\"]\n[White \"Engine\"]\n[WhiteElo \"3500\"]\n"
                              "[BlackElo \"3500\"]\n[Result \"1-0\"]\n\n1. e4 c5 1-0\n";
    const TempFolder folder;
    {
        auto writer = gamedb::DatabaseWriter::create(folder.path(), {"Online", "", 0, 0});
        ASSERT_TRUE(writer.ok());
        gamedb::GameSplitter splitter;
        const auto add = [&](std::string_view game) {
            (*writer)->add(gamedb::scanGame(game));
        };
        splitter.feed(games, add);
        splitter.finish(add);
        EXPECT_EQ((*writer)->finish(), "");
    }
    auto db = gamedb::Database::open(folder.path());
    ASSERT_TRUE(db.ok());
    EXPECT_FALSE((*db)->info().online);          // half online is not "mostly"
    EXPECT_TRUE((*db)->gamesAt({}, 10).empty()); // 3100 and 3500: engines over the board

    // Mostly online: the 3100 game is a human's; the 3500 one is still an engine's.
    const TempFolder second;
    {
        auto writer = gamedb::DatabaseWriter::create(second.path(), {"Online", "", 0, 0});
        ASSERT_TRUE(writer.ok());
        gamedb::GameSplitter splitter;
        const auto add = [&](std::string_view game) {
            (*writer)->add(gamedb::scanGame(game));
        };
        splitter.feed(games + "\n[Site \"https://www.chess.com/game/1\"]\n[Result \"*\"]\n\n1. "
                              "d4 *\n",
                      add);
        splitter.finish(add);
        EXPECT_EQ((*writer)->finish(), "");
    }
    auto onlineDb = gamedb::Database::open(second.path());
    ASSERT_TRUE(onlineDb.ok());
    EXPECT_TRUE((*onlineDb)->info().online);
    const auto top = (*onlineDb)->gamesAt({}, 10);
    ASSERT_EQ(top.size(), 2U); // the 3100 game and the unrated one
    EXPECT_EQ(top[0].white, "Fast");
}

TEST(GameDatabase, APlayersRecordByColour) {
    const TempFolder folder;
    {
        auto writer = gamedb::DatabaseWriter::create(folder.path(), {"Record", "", 0, 0});
        ASSERT_TRUE(writer.ok());
        gamedb::GameSplitter splitter;
        const auto add = [&](std::string_view game) {
            (*writer)->add(gamedb::scanGame(game));
        };
        splitter.feed(kGames, add);
        splitter.finish(add);
        EXPECT_EQ((*writer)->finish(), "");
    }
    auto db = gamedb::Database::open(folder.path());
    ASSERT_TRUE(db.ok());
    // Carlsen: won with White (vs Caruana), won with Black (vs Smith). Any case.
    const std::vector<std::string> carlsen{"CARLSEN, magnus"};
    const auto record = (*db)->record(carlsen);
    EXPECT_EQ(record.asWhite, (std::array<std::uint64_t, 3>{1, 0, 0}));
    EXPECT_EQ(record.asBlack, (std::array<std::uint64_t, 3>{1, 0, 0}));
    EXPECT_EQ(record.games(), 2U);
    const std::vector<std::string> both{"Jones", "Brown"}; // their draw counts once for each
    EXPECT_EQ((*db)->record(both).asWhite[1] + (*db)->record(both).asBlack[1], 2U);
}

TEST(GameScan, SpeedFromTimeControlOrEvent) {
    using gamedb::Speed;
    EXPECT_EQ(gamedb::speedOf("60", ""), Speed::Bullet);
    EXPECT_EQ(gamedb::speedOf("180+2", ""), Speed::Blitz); // 180 + 80
    EXPECT_EQ(gamedb::speedOf("600+5", ""), Speed::Rapid); // 600 + 200
    EXPECT_EQ(gamedb::speedOf("5400+30", ""), Speed::Classical);
    EXPECT_EQ(gamedb::speedOf("40/7200:3600", ""), Speed::Classical);
    EXPECT_EQ(gamedb::speedOf("1/86400", ""), Speed::Correspondence);
    EXPECT_EQ(gamedb::speedOf("-", "Rated Blitz game"), Speed::Blitz);
    EXPECT_EQ(gamedb::speedOf("", "Club championship"), Speed::Unknown);
}

TEST(GameDatabase, TheExplorerFiltersBySpeedYearPlayerAndRating) {
    const std::string games =
        "[White \"me\"]\n[Black \"x\"]\n[Date \"2020.01.01\"]\n[TimeControl \"180+2\"]\n"
        "[WhiteElo \"2000\"]\n[BlackElo \"2000\"]\n[Result \"1-0\"]\n\n1. e4 e5 1-0\n\n"
        "[White \"x\"]\n[Black \"Me\"]\n[Date \"2023.05.01\"]\n[TimeControl \"600\"]\n"
        "[Result \"0-1\"]\n\n1. e4 c5 0-1\n\n"
        "[White \"me\"]\n[Black \"y\"]\n[Date \"2024.02.02\"]\n[TimeControl \"60\"]\n"
        "[Result \"1/2-1/2\"]\n\n1. d4 d5 1/2-1/2\n";
    const TempFolder folder;
    {
        auto writer = gamedb::DatabaseWriter::create(folder.path(), {"Mine", "", 0, 0});
        ASSERT_TRUE(writer.ok());
        gamedb::GameSplitter splitter;
        const auto add = [&](std::string_view game) {
            (*writer)->add(gamedb::scanGame(game));
        };
        splitter.feed(games, add);
        splitter.finish(add);
        EXPECT_EQ((*writer)->finish(), "");
    }
    auto opened = gamedb::Database::open(folder.path());
    ASSERT_TRUE(opened.ok());
    const gamedb::Database& db = **opened;
    EXPECT_TRUE(db.hasAttributes());
    EXPECT_EQ(db.explore({}).games, 3U);

    gamedb::ExploreFilter blitz;
    blitz.speeds = 1U << static_cast<unsigned>(gamedb::Speed::Blitz);
    const auto onlyBlitz = db.explore({}, blitz);
    EXPECT_EQ(onlyBlitz.games, 1U);
    ASSERT_EQ(onlyBlitz.moves.size(), 1U);
    EXPECT_EQ(onlyBlitz.moves[0].san, "e4");
    EXPECT_EQ(onlyBlitz.whiteElo, 2000);

    gamedb::ExploreFilter recent;
    recent.fromYear = 2023;
    EXPECT_EQ(db.explore({}, recent).games, 2U);

    gamedb::ExploreFilter asWhite;
    asWhite.players = {"ME"}; // any case
    asWhite.side = gamedb::ExploreFilter::Side::White;
    const auto white = db.explore({}, asWhite);
    EXPECT_EQ(white.games, 2U); // e4 and d4
    EXPECT_EQ(white.moves.size(), 2U);
    asWhite.side = gamedb::ExploreFilter::Side::Black;
    EXPECT_EQ(db.explore({}, asWhite).games, 1U);

    gamedb::ExploreFilter names; // part of a name, any case, per colour
    names.white = "ME";
    EXPECT_EQ(db.explore({}, names).games, 2U);
    names.black = "y";
    EXPECT_EQ(db.explore({}, names).games, 1U); // me – y
    names.white.clear();
    names.black = "nobody";
    EXPECT_EQ(db.explore({}, names).games, 0U);

    gamedb::ExploreFilter rated;
    rated.minRating = 1900;
    EXPECT_EQ(db.explore({}, rated).games, 1U); // the others have no ratings
    EXPECT_EQ(db.gamesAt({}, 10, recent).size(), 2U);
}

TEST(GameDatabase, APlayersStatsByYearAndSpeed) {
    const std::string games =
        "[White \"me\"]\n[Black \"x\"]\n[Date \"2020.01.01\"]\n[TimeControl \"180+2\"]\n"
        "[Result \"1-0\"]\n\n1. e4 e5 1-0\n\n"
        "[White \"x\"]\n[Black \"me\"]\n[Date \"2023.05.01\"]\n[TimeControl \"600\"]\n"
        "[Result \"1-0\"]\n\n1. e4 c5 1-0\n\n"
        "[White \"me\"]\n[Black \"y\"]\n[Date \"2023.02.02\"]\n[TimeControl \"60\"]\n"
        "[Result \"1/2-1/2\"]\n\n1. d4 d5 1/2-1/2\n";
    const TempFolder folder;
    {
        auto writer = gamedb::DatabaseWriter::create(folder.path(), {"Me", "", 0, 0});
        ASSERT_TRUE(writer.ok());
        gamedb::GameSplitter splitter;
        const auto add = [&](std::string_view game) {
            (*writer)->add(gamedb::scanGame(game));
        };
        splitter.feed(games, add);
        splitter.finish(add);
        EXPECT_EQ((*writer)->finish(), "");
    }
    auto db = gamedb::Database::open(folder.path());
    ASSERT_TRUE(db.ok());
    const std::vector<std::string> me{"Me"};
    const auto all = (*db)->playerStats(me);
    EXPECT_EQ(all.total.asWhite, (std::array<std::uint64_t, 3>{1, 1, 0}));
    EXPECT_EQ(all.total.asBlack, (std::array<std::uint64_t, 3>{0, 0, 1}));
    ASSERT_EQ(all.years.size(), 2U);
    EXPECT_EQ(all.years.at(2023).games(), 2U);
    const auto rapid = (*db)->playerStats(me, 1U << static_cast<unsigned>(gamedb::Speed::Rapid));
    EXPECT_EQ(rapid.total.games(), 1U); // the 600-second loss with Black
    EXPECT_EQ(rapid.total.asBlack[2], 1U);
}

TEST(GameDatabase, DatesAsTypedBecomePgnDatePrefixes) {
    EXPECT_EQ(gamedb::pgnDatePrefix("2024"), "2024");
    EXPECT_EQ(gamedb::pgnDatePrefix("2024-5"), "2024.05");
    EXPECT_EQ(gamedb::pgnDatePrefix("2024.05.12"), "2024.05.12");
    EXPECT_EQ(gamedb::pgnDatePrefix("12.05.2024"), "2024.05.12"); // day first
    EXPECT_EQ(gamedb::pgnDatePrefix(" 2024 / 11 "), "2024.11");
    EXPECT_FALSE(gamedb::pgnDatePrefix("24").has_value());
    EXPECT_FALSE(gamedb::pgnDatePrefix("2024.13").has_value());
    EXPECT_FALSE(gamedb::pgnDatePrefix("May 2024").has_value());
    EXPECT_FALSE(gamedb::pgnDatePrefix("").has_value());
    // A range takes whole years, months and days; unknown dates only without limits.
    EXPECT_TRUE(gamedb::dateInRange("2024.12.31", "2024", "2024"));
    EXPECT_TRUE(gamedb::dateInRange("2024.??.??", "2023", "2024"));
    EXPECT_FALSE(gamedb::dateInRange("2025.01.01", "2024", "2024"));
    EXPECT_FALSE(gamedb::dateInRange("2024.04.30", "2024.05", ""));
    EXPECT_TRUE(gamedb::dateInRange("2024.05.01", "2024.05", ""));
    EXPECT_FALSE(gamedb::dateInRange("????.??.??", "", "2024"));
    EXPECT_TRUE(gamedb::dateInRange("????.??.??", "", ""));
}

TEST(GameDatabase, GamesCanBeFoundByTextAndDatesInAnyOrder) {
    const std::string games =
        "[White \"Carol\"]\n[Black \"Bob\"]\n[Date \"2021.03.01\"]\n[WhiteElo \"2100\"]\n"
        "[Result \"1-0\"]\n\n1. e4 e5 1-0\n\n"
        "[White \"alice\"]\n[Black \"Carol\"]\n[Date \"2023.07.15\"]\n[WhiteElo \"1900\"]\n"
        "[Result \"0-1\"]\n\n1. d4 d5 0-1\n\n"
        "[White \"Bob\"]\n[Black \"Dave\"]\n[Date \"2023.01.02\"]\n[WhiteElo \"2300\"]\n"
        "[Result \"1/2-1/2\"]\n\n1. c4 c5 1/2-1/2\n\n"
        "[White \"Eve\"]\n[Black \"Carol\"]\n[Date \"????.??.??\"]\n"
        "[Result \"1-0\"]\n\n1. Nf3 Nf6 1-0\n";
    const TempFolder folder;
    {
        auto writer = gamedb::DatabaseWriter::create(folder.path(), {"Test", "", 0, 0});
        ASSERT_TRUE(writer.ok());
        gamedb::GameSplitter splitter;
        const auto add = [&](std::string_view game) {
            (*writer)->add(gamedb::scanGame(game));
        };
        splitter.feed(games, add);
        splitter.finish(add);
        EXPECT_EQ((*writer)->finish(), "");
    }
    auto db = gamedb::Database::open(folder.path());
    ASSERT_TRUE(db.ok());
    const auto whites = [&](const gamedb::GameQuery& query) {
        std::vector<std::string> names;
        for (const auto& game : (*db)->find(query, 10)) {
            names.push_back(game.white);
        }
        return names;
    };
    gamedb::GameQuery query;
    EXPECT_EQ(whites(query), (std::vector<std::string>{"Carol", "alice", "Bob", "Eve"}));
    query.order = gamedb::GameQuery::Order::White; // names in any case
    EXPECT_EQ(whites(query), (std::vector<std::string>{"alice", "Bob", "Carol", "Eve"}));
    query.descending = true;
    EXPECT_EQ(whites(query), (std::vector<std::string>{"Eve", "Carol", "Bob", "alice"}));
    query.order = gamedb::GameQuery::Order::WhiteElo;
    EXPECT_EQ(whites(query).front(), "Bob");
    // 2023 only; the unknown date never fits a range.
    query = {};
    query.fromDate = "2023";
    query.toDate = "2023";
    query.order = gamedb::GameQuery::Order::Date;
    EXPECT_EQ(whites(query), (std::vector<std::string>{"Bob", "alice"}));
    // Carol's games, newest first (an unknown date last); then only up to 2022.
    query = {};
    query.text = "carol";
    EXPECT_EQ(whites(query), (std::vector<std::string>{"alice", "Carol", "Eve"}));
    query.toDate = "2022";
    EXPECT_EQ(whites(query), (std::vector<std::string>{"Carol"}));

    // Made with the indexes for sorting; preparing it again is harmless.
    EXPECT_TRUE((*db)->hasSortIndexes());
    EXPECT_EQ(gamedb::addSortIndexes(folder.path()), "");
    // A query cancelled before it runs: stopped, nothing (to keep) comes back.
    const std::atomic<bool> stop{true};
    query = {};
    query.order = gamedb::GameQuery::Order::Event;
    EXPECT_TRUE((*db)->find(query, 10, &stop).empty());
    const std::atomic<bool> go{false};
    EXPECT_EQ((*db)->find(query, 10, &go).size(), 4U);
}
