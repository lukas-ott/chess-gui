// Speed of the operations that decide how fast PGN files and repertoires open (M3, M5):
// reading FEN, generating moves, writing and reading SAN, and building a game tree.
// Run from the release preset:  ./build/release/bench/chessgui_bench

#include "chessgui/core/game.hpp"
#include "chessgui/core/position.hpp"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace chessgui::core;

/// A fixed, reproducible game of up to 120 plies, picked by a simple pseudo-random sequence.
/// Real games would do too; this avoids typing one in by hand.
struct SampleGame {
    std::vector<Move> moves;
    std::vector<std::string> sans;
};

const SampleGame& sampleGame() {
    static const SampleGame game = [] {
        SampleGame result;
        Position position;
        std::uint32_t seed = 12345;
        for (int ply = 0; ply < 120; ++ply) {
            const auto moves = position.legalMoves();
            if (moves.empty()) {
                break;
            }
            seed = (seed * 1103515245U) + 12345U;
            const Move move = moves[(seed >> 16U) % moves.size()];
            result.sans.push_back(position.san(move));
            result.moves.push_back(move);
            position.play(move);
        }
        return result;
    }();
    return game;
}

void fenParse(benchmark::State& state) {
    constexpr std::string_view kFen =
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    for ([[maybe_unused]] const auto _ : state) {
        auto position = Position::fromFen(kFen);
        benchmark::DoNotOptimize(position);
    }
}

void legalMoves(benchmark::State& state) {
    const Position position;
    for ([[maybe_unused]] const auto _ : state) {
        auto moves = position.legalMoves();
        benchmark::DoNotOptimize(moves);
    }
}

void sanWrite(benchmark::State& state) {
    const auto& game = sampleGame();
    for ([[maybe_unused]] const auto _ : state) {
        Position position;
        for (const Move& move : game.moves) {
            auto san = position.san(move);
            benchmark::DoNotOptimize(san);
            position.play(move);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(game.moves.size()));
}

void sanParse(benchmark::State& state) {
    const auto& game = sampleGame();
    for ([[maybe_unused]] const auto _ : state) {
        Position position;
        for (const auto& san : game.sans) {
            const auto move = position.parseSan(san);
            position.play(*move);
        }
        benchmark::DoNotOptimize(position);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(game.sans.size()));
}

void gameBuildFromSan(benchmark::State& state) {
    const auto& game = sampleGame();
    const std::vector<std::string_view> sans(game.sans.begin(), game.sans.end());
    for ([[maybe_unused]] const auto _ : state) {
        Game tree;
        auto last = tree.addLine(Game::root(), sans);
        benchmark::DoNotOptimize(last);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(sans.size()));
}

} // namespace

// The BENCHMARK macro registers through a global variable, which is how the library works.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
BENCHMARK(fenParse);
BENCHMARK(legalMoves);
BENCHMARK(sanWrite);
BENCHMARK(sanParse);
BENCHMARK(gameBuildFromSan);
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
