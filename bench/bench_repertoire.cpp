// Repertoires: building the merged tree (what opening a repertoire waits for, in the
// background), following a game through it, and reading the opening names. Spec: a ~50k-position
// repertoire ready in < 300 ms.
// Run from the release preset:  ./build/release/bench/kiebitz_bench --benchmark_filter=repertoire

#include "kiebitz/repertoire/openings.hpp"
#include "kiebitz/repertoire/tree.hpp"

#include <benchmark/benchmark.h>

#include <fstream>
#include <functional>
#include <sstream>

namespace {

using namespace kiebitz;

/// A repertoire-sized game: a main line with an alternative at every move, and the same again
/// inside the alternatives (depth 2: about 50,000 positions).
core::Game bigRepertoire() {
    core::Game game;
    std::uint32_t seed = 7;
    const std::function<void(core::NodeId, core::Position, int, int)> grow =
        [&](core::NodeId at, core::Position position, int plies, int depth) {
            for (int ply = 0; ply < plies; ++ply) {
                const auto moves = position.legalMoves();
                if (moves.size() < 2) {
                    return;
                }
                seed = (seed * 1103515245U) + 12345U;
                const core::Move main = moves[(seed >> 16U) % moves.size()];
                const core::Move alt = moves[((seed >> 16U) + 1) % moves.size()];
                const core::NodeId next = game.addMove(at, position, main);
                core::Position altPosition = position;
                const core::NodeId altNode = game.addMove(at, altPosition, alt);
                altPosition.play(alt);
                if (depth > 0) {
                    grow(altNode, altPosition, 12, depth - 1);
                }
                position.play(main);
                at = next;
            }
        };
    grow(core::Game::root(), core::Position(), 200, 2);
    return game;
}

void repertoireBuild(benchmark::State& state) {
    const core::Game game = bigRepertoire();
    std::size_t positions = 0;
    for ([[maybe_unused]] const auto _ : state) {
        repertoire::Tree tree(core::Color::White);
        tree.add(game, 0, 0);
        positions = tree.size();
        benchmark::DoNotOptimize(tree);
    }
    state.counters["positions"] = static_cast<double>(positions);
}

void repertoireFollow(benchmark::State& state) {
    const core::Game game = bigRepertoire();
    repertoire::Tree tree(core::Color::White);
    tree.add(game, 0, 0);
    std::vector<core::Move> line;
    for (const core::NodeId id : game.mainLine()) {
        if (id != core::Game::root()) {
            line.push_back(game.node(id).move);
        }
    }
    for ([[maybe_unused]] const auto _ : state) {
        benchmark::DoNotOptimize(tree.follow(core::Position(), line));
    }
}

void repertoireStats(benchmark::State& state) {
    const core::Game game = bigRepertoire();
    repertoire::Tree tree(core::Color::White);
    tree.add(game, 0, 0);
    for ([[maybe_unused]] const auto _ : state) {
        benchmark::DoNotOptimize(tree.stats());
    }
}

void repertoireOpeningNames(benchmark::State& state) {
    const std::ifstream file(KIEBITZ_OPENINGS_TSV);
    std::stringstream text;
    text << file.rdbuf();
    const std::string tsv = text.str();
    for ([[maybe_unused]] const auto _ : state) {
        benchmark::DoNotOptimize(repertoire::OpeningNames::fromTsv(tsv));
    }
}

} // namespace

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
BENCHMARK(repertoireBuild)->Unit(benchmark::kMillisecond);
BENCHMARK(repertoireFollow)->Unit(benchmark::kMicrosecond);
BENCHMARK(repertoireStats)->Unit(benchmark::kMillisecond);
BENCHMARK(repertoireOpeningNames)->Unit(benchmark::kMillisecond);
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
