// The library cache: what a start with an unchanged library costs (every file's index from the
// cache instead of reading and indexing every file). Spec: the library ready in < 1 s.
// Run from the release preset:  ./build/release/bench/kiebitz_bench --benchmark_filter=library

#include "kiebitz/library/index_cache.hpp"

#include <benchmark/benchmark.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace kiebitz;

/// The index of one file with `count` games, with headers like a real database's.
std::vector<pgn::GameEntry> sampleIndex(std::size_t count) {
    std::vector<pgn::GameEntry> games;
    games.reserve(count);
    std::size_t offset = 0;
    for (std::size_t i = 0; i < count; ++i) {
        games.push_back({offset,
                         1800,
                         {{"Event", "Tata Steel Masters"},
                          {"Site", "Wijk aan Zee NED"},
                          {"Date", "2024.01.2" + std::to_string(i % 10)},
                          {"Round", std::to_string(i % 13)},
                          {"White", "Player " + std::to_string(i)},
                          {"Black", "Player " + std::to_string(i + 1)},
                          {"Result", "1/2-1/2"},
                          {"ECO", "C65"}}});
        offset += 1801;
    }
    return games;
}

std::string cachePath() {
    return (std::filesystem::temp_directory_path() / "kiebitz-bench-cache.sqlite").string();
}

/// A start with 300 unchanged files of 40 games each (12,000 games): every index from the cache.
void libraryStartFromCache(benchmark::State& state) {
    std::filesystem::remove(cachePath());
    auto cache = library::IndexCache::open(cachePath());
    const auto index = sampleIndex(40);
    std::vector<std::string> paths;
    for (int i = 0; i < 300; ++i) {
        paths.push_back("/Users/me/Documents/Kiebitz/file" + std::to_string(i) + ".pgn");
        (*cache)->store(paths.back(), {1000, 1}, index);
    }
    std::size_t games = 0;
    for ([[maybe_unused]] const auto _ : state) {
        games = 0;
        for (const auto& path : paths) {
            games += (*cache)->find(path, {1000, 1})->size();
        }
    }
    state.counters["games"] = static_cast<double>(games);
    cache->reset();
    std::filesystem::remove(cachePath());
}

/// One big database's index (10,000 games) in and out of its stored form.
void libraryEncodeIndex(benchmark::State& state) {
    const auto index = sampleIndex(10'000);
    for ([[maybe_unused]] const auto _ : state) {
        benchmark::DoNotOptimize(library::encodeIndex(index));
    }
}

void libraryDecodeIndex(benchmark::State& state) {
    const std::string bytes = library::encodeIndex(sampleIndex(10'000));
    for ([[maybe_unused]] const auto _ : state) {
        benchmark::DoNotOptimize(library::decodeIndex(bytes));
    }
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(bytes.size()));
}

} // namespace

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
BENCHMARK(libraryStartFromCache)->Unit(benchmark::kMillisecond);
BENCHMARK(libraryEncodeIndex)->Unit(benchmark::kMillisecond);
BENCHMARK(libraryDecodeIndex)->Unit(benchmark::kMillisecond);
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
