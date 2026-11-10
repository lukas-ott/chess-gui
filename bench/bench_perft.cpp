// Move generation throughput. Run from the release preset:
//   ./build/release/bench/kiebitz_bench

#include "kiebitz/core/perft.hpp"

#include <benchmark/benchmark.h>

#include <cstdint>

namespace {

void perftStartPosition(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    std::uint64_t nodes = 0;
    for ([[maybe_unused]] const auto _ : state) {
        nodes = kiebitz::core::perft(kiebitz::core::kStartFen, depth);
        benchmark::DoNotOptimize(nodes);
    }
    state.counters["nodes/s"] =
        benchmark::Counter(static_cast<double>(nodes) * static_cast<double>(state.iterations()),
                           benchmark::Counter::kIsRate);
}

// The BENCHMARK macro registers through a global variable, which is how the library works.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
BENCHMARK(perftStartPosition)->Arg(4)->Arg(5)->Unit(benchmark::kMillisecond);

} // namespace
