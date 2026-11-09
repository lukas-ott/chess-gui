// Move generation throughput. Run from the release preset:
//   ./build/release/bench/chessgui_bench

#include "chessgui/core/perft.hpp"

#include <benchmark/benchmark.h>

#include <cstdint>

namespace {

void BM_PerftStartPosition(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    std::uint64_t nodes = 0;
    for (auto _ : state) {
        nodes = chessgui::core::perft(chessgui::core::kStartFen, depth);
        benchmark::DoNotOptimize(nodes);
    }
    state.counters["nodes/s"] =
        benchmark::Counter(static_cast<double>(nodes) * static_cast<double>(state.iterations()),
                           benchmark::Counter::kIsRate);
}

BENCHMARK(BM_PerftStartPosition)->Arg(4)->Arg(5)->Unit(benchmark::kMillisecond);

} // namespace
