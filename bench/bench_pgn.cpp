// PGN speed: the game index (what opening a big file waits for) and full parsing (what opening
// one game waits for). Spec targets: a ~50k-position repertoire in < 300 ms (with cache, M5).
// Run from the release preset:  ./build/release/bench/kiebitz_bench --benchmark_filter=pgn

#include "kiebitz/core/game.hpp"
#include "kiebitz/pgn/pgn.hpp"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>

namespace {

using namespace kiebitz;

/// A reproducible annotated game of ~120 plies with a short variation at every 10th move and a
/// comment every 7th, built by pseudo-random legal moves.
core::Game sampleGame(std::uint32_t seed) {
    core::Game game;
    core::Position position;
    core::NodeId current = core::Game::root();
    for (int ply = 0; ply < 120; ++ply) {
        const auto moves = position.legalMoves();
        if (moves.empty()) {
            break;
        }
        seed = (seed * 1103515245U) + 12345U;
        const core::Move move = moves[(seed >> 16U) % moves.size()];
        if (ply % 10 == 5 && moves.size() > 1) { // an alternative, two plies deep
            const core::Move alt = moves[((seed >> 16U) + 1) % moves.size()];
            core::Position altPosition = position;
            const core::NodeId altNode = game.addMove(current, altPosition, alt);
            altPosition.play(alt);
            const auto replies = altPosition.legalMoves();
            if (!replies.empty()) {
                game.addMove(altNode, altPosition, replies.front());
            }
        }
        current = game.addMove(current, position, move);
        game.makeMainLine(current);
        if (ply % 7 == 3) {
            game.setComment(current, "An idea worth remembering here");
        }
        position.play(move);
    }
    return game;
}

const std::string& sampleFile() {
    static const std::string text = [] {
        std::string result;
        for (std::uint32_t i = 0; i < 200; ++i) {
            result += pgn::writeGame(sampleGame(i + 1));
            result += '\n';
        }
        return result;
    }();
    return text;
}

void pgnIndex(benchmark::State& state) {
    const std::string& text = sampleFile();
    for ([[maybe_unused]] const auto _ : state) {
        auto games = pgn::indexGames(text);
        benchmark::DoNotOptimize(games);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(text.size()));
}

void pgnParseAll(benchmark::State& state) {
    const std::string& text = sampleFile();
    std::size_t nodes = 0;
    for ([[maybe_unused]] const auto _ : state) {
        auto games = pgn::parseAll(text);
        nodes = 0;
        for (const auto& game : *games) {
            nodes += game.nodeCount();
        }
        benchmark::DoNotOptimize(games);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(text.size()));
    state.counters["positions/s"] =
        benchmark::Counter(static_cast<double>(nodes) * static_cast<double>(state.iterations()),
                           benchmark::Counter::kIsRate);
}

void pgnWrite(benchmark::State& state) {
    const core::Game game = sampleGame(7);
    for ([[maybe_unused]] const auto _ : state) {
        auto text = pgn::writeGame(game);
        benchmark::DoNotOptimize(text);
    }
}

} // namespace

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
BENCHMARK(pgnIndex)->Unit(benchmark::kMillisecond);
BENCHMARK(pgnParseAll)->Unit(benchmark::kMillisecond);
BENCHMARK(pgnWrite)->Unit(benchmark::kMicrosecond);
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
