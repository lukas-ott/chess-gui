// Puzzles: how fast the Lichess puzzle database (~5.7 million lines) imports, and how fast the
// next puzzle is there. Spec: the import takes well under a minute; a puzzle shows at once
// (microseconds). The lines here are real ones repeated with made-up ids and ratings.
// Run from the release preset:  ./build/release/bench/kiebitz_bench --benchmark_filter=puzzle

#include "kiebitz/puzzles/store.hpp"

#include <benchmark/benchmark.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>

namespace {

using namespace kiebitz;

constexpr int kPuzzles = 500000;

const std::string& csvText() {
    static const std::string text = [] {
        static constexpr std::array<const char*, 3> kLines{
            ",r6k/pp2r2p/4Rp1Q/3p4/8/1N1P2R1/PqP2bPP/7K b - - 0 24,f2g3 e6e7 b2b1 b3c1 b1c1 h6c1,",
            ",5rk1/1p3ppp/pq3b2/8/8/1P1Q1N2/P4PPP/3R2K1 w - - 2 27,d3d6 f8d8 d6d8 f6d8,",
            ",r4rk1/pp3ppp/8/2bqp2b/3N4/P2Bn2P/1PPN1P2/R1BQ1RK1 w - - 1 15,d1h5 d5g2,"};
        std::string out = "PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,"
                          "GameUrl,OpeningTags\n";
        std::mt19937 random(5); // NOLINT(bugprone-random-generator-seed): the same lines each run
        for (int i = 0; i < kPuzzles; ++i) {
            out += "p" + std::to_string(i) + kLines[static_cast<std::size_t>(i) % kLines.size()] +
                   std::to_string(600 + (random() % 2400)) +
                   ",75,95,1000,crushing hangingPiece long middlegame,https://lichess.org/x,\n";
        }
        return out;
    }();
    return text;
}

std::string benchPath() {
    return (std::filesystem::temp_directory_path() / "kiebitz_bench_puzzles").string();
}

void puzzleImport(benchmark::State& state) {
    const std::string& text = csvText();
    for ([[maybe_unused]] const auto _ : state) {
        std::size_t at = 0;
        auto result = puzzles::importLichess(
            [&](char* buffer, std::size_t size) {
                const std::size_t n = std::min(size, text.size() - at);
                std::copy_n(text.data() + at, n, buffer);
                at += n;
                return n;
            },
            benchPath());
        benchmark::DoNotOptimize(result.puzzles);
    }
    state.counters["puzzles/s"] =
        benchmark::Counter(static_cast<double>(kPuzzles) * static_cast<double>(state.iterations()),
                           benchmark::Counter::kIsRate);
}
BENCHMARK(puzzleImport)->Unit(benchmark::kMillisecond)->Iterations(2)->UseRealTime();

void puzzlePickAndLoad(benchmark::State& state) {
    const auto db = puzzles::PuzzleDb::open(benchPath());
    if (db == nullptr) {
        state.SkipWithError("run puzzleImport first");
        return;
    }
    std::mt19937_64 random(7);
    for ([[maybe_unused]] const auto _ : state) {
        const auto index = db->pick(1400, 1800, 1, random);
        benchmark::DoNotOptimize(db->puzzle(*index));
    }
}
BENCHMARK(puzzlePickAndLoad)->Unit(benchmark::kMicrosecond);

/// The real Lichess file, when KIEBITZ_PUZZLE_CSV names it (lichess_db_puzzle.csv.zst).
void puzzleImportLichess(benchmark::State& state) {
    const char* csv = std::getenv("KIEBITZ_PUZZLE_CSV");
    if (csv == nullptr) {
        state.SkipWithError("set KIEBITZ_PUZZLE_CSV to lichess_db_puzzle.csv.zst");
        return;
    }
    const std::string path = benchPath() + "_lichess";
    for ([[maybe_unused]] const auto _ : state) {
        std::FILE* in = std::fopen(csv, "rb");
        if (in == nullptr) {
            state.SkipWithError("can't read KIEBITZ_PUZZLE_CSV");
            return;
        }
        auto result = puzzles::importLichess(
            [&](char* buffer, std::size_t size) { return std::fread(buffer, 1, size, in); }, path);
        std::fclose(in);
        if (!result.error.empty()) {
            state.SkipWithError(result.error + " skipped " + std::to_string(result.skipped));
            return;
        }
        state.counters["puzzles"] = static_cast<double>(result.puzzles);
        state.counters["skipped"] = static_cast<double>(result.skipped);
        state.counters["MB"] =
            static_cast<double>(std::filesystem::file_size(path)) / (1024.0 * 1024.0);
    }
    const auto db = puzzles::PuzzleDb::open(path);
    std::size_t unreadable = 0;
    for (std::size_t i = 0; i < db->size(); i += 101) {
        unreadable += db->puzzle(i).has_value() ? 0 : 1;
    }
    state.counters["unreadable/101"] = static_cast<double>(unreadable);
    state.counters["themes"] = static_cast<double>(db->themes().size());
}
BENCHMARK(puzzleImportLichess)->Unit(benchmark::kMillisecond)->Iterations(1)->UseRealTime();

} // namespace
