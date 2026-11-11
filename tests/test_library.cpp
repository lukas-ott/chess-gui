#include "kiebitz/library/index_cache.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

using kiebitz::library::decodeIndex;
using kiebitz::library::encodeIndex;
using kiebitz::library::FileStamp;
using kiebitz::library::IndexCache;
using kiebitz::pgn::GameEntry;

namespace {

std::vector<GameEntry> sampleGames() {
    return {
        {0, 120, {{"White", "Carlsen, Magnus"}, {"Black", "Nepomniachtchi"}, {"Result", "1-0"}}},
        {121, 5'000'000'000ULL, {{"Event", "Ålesund Open — Runde 3"}, {"Empty", ""}}},
        {5'000'000'121ULL, 0, {}},
    };
}

void expectSame(const std::vector<GameEntry>& a, const std::vector<GameEntry>& b) {
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].offset, b[i].offset);
        EXPECT_EQ(a[i].length, b[i].length);
        EXPECT_EQ(a[i].headers, b[i].headers);
    }
}

/// A fresh, empty folder for one test's cache file.
class TempDir {
public:
    TempDir() {
        std::random_device random;
        m_path = std::filesystem::temp_directory_path() /
                 ("kiebitz-cache-test-" + std::to_string(random()));
        std::filesystem::create_directories(m_path);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
    ~TempDir() { std::filesystem::remove_all(m_path); }
    [[nodiscard]] std::string file(const char* name) const { return (m_path / name).string(); }

private:
    std::filesystem::path m_path;
};

} // namespace

TEST(IndexEncoding, RoundTrips) {
    const auto games = sampleGames();
    const auto decoded = decodeIndex(encodeIndex(games));
    ASSERT_TRUE(decoded.has_value());
    expectSame(*decoded, games);
}

TEST(IndexEncoding, EmptyIndexRoundTrips) {
    const auto decoded = decodeIndex(encodeIndex({}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->empty());
}

TEST(IndexEncoding, RejectsDamage) {
    const std::string bytes = encodeIndex(sampleGames());
    for (std::size_t cut = 0; cut < bytes.size(); ++cut) { // every truncation
        EXPECT_FALSE(decodeIndex(std::string_view(bytes).substr(0, cut)).has_value()) << cut;
    }
    EXPECT_FALSE(decodeIndex(bytes + "x").has_value()); // trailing bytes
    EXPECT_FALSE(decodeIndex("\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x01").has_value());
    EXPECT_FALSE(decodeIndex("\x7f").has_value()); // claims 127 games, has none
}

TEST(IndexCache, StoresAndFinds) {
    const TempDir dir;
    auto cache = IndexCache::open(dir.file("cache.sqlite"));
    ASSERT_TRUE(cache) << cache.error().message;
    const FileStamp stamp{1234, 1'700'000'000'000};
    EXPECT_FALSE((*cache)->find("/lib/a.pgn", stamp).has_value());
    (*cache)->store("/lib/a.pgn", stamp, sampleGames());
    const auto found = (*cache)->find("/lib/a.pgn", stamp);
    ASSERT_TRUE(found.has_value());
    expectSame(*found, sampleGames());
}

TEST(IndexCache, ChangedFileIsNotFound) {
    const TempDir dir;
    auto cache = IndexCache::open(dir.file("cache.sqlite"));
    ASSERT_TRUE(cache);
    (*cache)->store("/lib/a.pgn", {100, 5}, sampleGames());
    EXPECT_FALSE((*cache)->find("/lib/a.pgn", {101, 5}).has_value()); // size changed
    EXPECT_FALSE((*cache)->find("/lib/a.pgn", {100, 6}).has_value()); // modified since
    (*cache)->store("/lib/a.pgn", {101, 6}, {});                      // re-indexed
    const auto found = (*cache)->find("/lib/a.pgn", {101, 6});
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->empty());
    EXPECT_EQ((*cache)->size(), 1U);
}

TEST(IndexCache, SurvivesReopening) {
    const TempDir dir;
    {
        auto cache = IndexCache::open(dir.file("cache.sqlite"));
        ASSERT_TRUE(cache);
        (*cache)->store("/lib/a.pgn", {1, 2}, sampleGames());
    }
    auto cache = IndexCache::open(dir.file("cache.sqlite"));
    ASSERT_TRUE(cache);
    EXPECT_TRUE((*cache)->find("/lib/a.pgn", {1, 2}).has_value());
}

TEST(IndexCache, ForgetsDeletedFilesOfThatFolderOnly) {
    const TempDir dir;
    auto cache = IndexCache::open(dir.file("cache.sqlite"));
    ASSERT_TRUE(cache);
    for (const char* path :
         {"/lib/a.pgn", "/lib/sub/b.pgn", "/lib/c.pgn", "/other/d.pgn", "/library2/e.pgn"}) {
        (*cache)->store(path, {1, 1}, {});
    }
    const std::vector<std::string> keep{"/lib/a.pgn", "/lib/sub/b.pgn"};
    (*cache)->forgetOthers("/lib", keep);
    EXPECT_TRUE((*cache)->find("/lib/a.pgn", {1, 1}).has_value());
    EXPECT_TRUE((*cache)->find("/lib/sub/b.pgn", {1, 1}).has_value());
    EXPECT_FALSE((*cache)->find("/lib/c.pgn", {1, 1}).has_value());
    EXPECT_TRUE((*cache)->find("/other/d.pgn", {1, 1}).has_value());    // another folder
    EXPECT_TRUE((*cache)->find("/library2/e.pgn", {1, 1}).has_value()); // a prefix, not a parent
}

TEST(IndexCache, DamagedFileStartsAfresh) {
    const TempDir dir;
    const std::string path = dir.file("cache.sqlite");
    std::ofstream(path) << "this is not a database, just some text that is long enough";
    auto cache = IndexCache::open(path);
    ASSERT_TRUE(cache) << cache.error().message;
    EXPECT_EQ((*cache)->size(), 0U);
    (*cache)->store("/lib/a.pgn", {1, 1}, sampleGames());
    EXPECT_TRUE((*cache)->find("/lib/a.pgn", {1, 1}).has_value());
}

TEST(IndexCache, UnwritableLocationFails) {
    const auto cache = IndexCache::open("/nonexistent-folder/for/sure/cache.sqlite");
    EXPECT_FALSE(cache);
}
