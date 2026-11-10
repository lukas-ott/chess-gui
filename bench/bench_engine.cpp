// Engine output: Stockfish prints dozens of "info" lines a second with 3–5 lines at once, and
// each shown line is turned into SAN for the panel. Both must stay far below a frame (16 ms).
// Run from the release preset:  ./build/release/bench/kiebitz_bench --benchmark_filter=engine

#include "kiebitz/engine/uci.hpp"

#include <benchmark/benchmark.h>

#include <string>

namespace {

using namespace kiebitz;

// A typical deep Stockfish line (depth 30, 20-move PV).
constexpr const char* kInfoLine =
    "info depth 30 seldepth 41 multipv 1 score cp 31 wdl 61 917 22 nodes 391202873 nps 2093418 "
    "hashfull 998 tbhits 0 time 186873 pv e2e4 e7e5 g1f3 b8c6 f1b5 g8f6 e1g1 f6e4 f1e1 e4d6 f3e5 "
    "f8e7 b5f1 c6e5 e1e5 e8g8 d2d4 e7f6 e5e1 f8e8";

void engineParseInfo(benchmark::State& state) {
    const std::string line = kInfoLine;
    for ([[maybe_unused]] const auto _ : state) {
        auto info = engine::parseInfo(line);
        benchmark::DoNotOptimize(info);
    }
}
BENCHMARK(engineParseInfo);

void enginePvToMoves(benchmark::State& state) {
    const auto info = engine::parseInfo(kInfoLine).value();
    const core::Position start;
    for ([[maybe_unused]] const auto _ : state) {
        auto moves = engine::pvMoves(start, info.pv);
        benchmark::DoNotOptimize(moves);
    }
}
BENCHMARK(enginePvToMoves);

} // namespace
