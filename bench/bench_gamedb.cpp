// Game databases: how fast a big PGN becomes a database, and how fast the explorer answers.
// Spec: Gigabase (~10 million games) imports in minutes, and a line's statistics show in well
// under a second. The games here are made up (an opening "book" of a few choices per move for
// the first 12 plies, then random moves), about as long as real games.
// Run from the release preset:  ./build/release/bench/kiebitz_bench --benchmark_filter=gamedb

#include "kiebitz/gamedb/database.hpp"
#include "kiebitz/gamedb/import.hpp"
#include "kiebitz/gamedb/scan.hpp"

#include <benchmark/benchmark.h>

#include <filesystem>
#include <random>
#include <string>

namespace {

using namespace kiebitz;

constexpr int kGames = 50000;

/// kGames made-up games as PGN.
const std::string& pgnText() {
    static const std::string text = [] {
        std::string out;
        std::mt19937 random(3); // NOLINT(bugprone-random-generator-seed): the same games each run
        for (int g = 0; g < kGames; ++g) {
            out += "[Event \"Bench " + std::to_string(g % 300) + "\"]\n[White \"Player " +
                   std::to_string(random() % 20000) + "\"]\n[Black \"Player " +
                   std::to_string(random() % 20000) + "\"]\n[WhiteElo \"" +
                   std::to_string(1800 + (random() % 1000)) + "\"]\n[Result \"1-0\"]\n\n";
            core::Position position;
            const int plies = 40 + static_cast<int>(random() % 80);
            for (int ply = 0; ply < plies; ++ply) {
                const auto moves = position.legalMoves();
                if (moves.empty()) {
                    break;
                }
                const auto pick = ply < 12 ? random() % std::min<std::size_t>(3, moves.size())
                                           : random() % moves.size();
                if (ply % 2 == 0) {
                    out += std::to_string((ply / 2) + 1) + ". ";
                }
                out += position.san(moves[pick]) + " ";
                position.play(moves[pick]);
            }
            out += "1-0\n\n";
        }
        return out;
    }();
    return text;
}

std::string freshFolder(const char* name) {
    const auto folder = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    return folder.string();
}

void gamedbScan(benchmark::State& state) {
    const std::string& text = pgnText();
    gamedb::GameSplitter splitter;
    std::vector<std::string> owned;
    splitter.feed(text, [&](std::string_view game) { owned.emplace_back(game); });
    splitter.finish([&](std::string_view game) { owned.emplace_back(game); });
    std::size_t plies = 0;
    for ([[maybe_unused]] const auto _ : state) {
        for (const auto& game : owned) {
            plies += gamedb::scanGame(game).moves.size();
        }
    }
    state.counters["games/s"] = benchmark::Counter(
        static_cast<double>(owned.size() * state.iterations()), benchmark::Counter::kIsRate);
    state.counters["plies/s"] =
        benchmark::Counter(static_cast<double>(plies), benchmark::Counter::kIsRate);
}
BENCHMARK(gamedbScan)->Unit(benchmark::kMillisecond)->Iterations(1)->UseRealTime();

void gamedbImport(benchmark::State& state) {
    const std::string& text = pgnText();
    for ([[maybe_unused]] const auto _ : state) {
        state.PauseTiming();
        const std::string fresh = freshFolder("kiebitz-bench-gamedb");
        state.ResumeTiming();
        auto writer = gamedb::DatabaseWriter::create(fresh, {"Bench", "", 0, 0});
        std::size_t at = 0;
        gamedb::importPgn(**writer, [&](char* buffer, std::size_t size) {
            const std::size_t n = std::min(size, text.size() - at);
            std::copy_n(text.data() + at, n, buffer);
            at += n;
            return n;
        });
        (*writer)->finish();
    }
    state.counters["games/s"] = benchmark::Counter(static_cast<double>(kGames * state.iterations()),
                                                   benchmark::Counter::kIsRate);
    state.counters["MB/s"] = benchmark::Counter(
        static_cast<double>(text.size() * state.iterations()) / 1e6, benchmark::Counter::kIsRate);
}
BENCHMARK(gamedbImport)->Unit(benchmark::kMillisecond)->Iterations(2)->UseRealTime();

void gamedbExplore(benchmark::State& state) {
    // Uses the database the import benchmark left behind.
    auto db = gamedb::Database::open(
        (std::filesystem::temp_directory_path() / "kiebitz-bench-gamedb").string());
    if (!db.ok()) {
        state.SkipWithError("run gamedbImport first");
        return;
    }
    const core::Position position;
    const auto e4 = position.parseSan("e4").value();
    const std::vector<core::Move> line{e4};
    for ([[maybe_unused]] const auto _ : state) {
        benchmark::DoNotOptimize((*db)->explore({}));
        benchmark::DoNotOptimize((*db)->explore(line));
    }
}
BENCHMARK(gamedbExplore)->Unit(benchmark::kMicrosecond);

} // namespace
