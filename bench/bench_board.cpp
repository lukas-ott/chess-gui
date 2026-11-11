// How long a full board repaint takes (spec: move input to redraw < 16 ms, one frame at 60 Hz).
// Paints offscreen into an image, the same drawing work the screen does.
// Run from the release preset:  ./build/release/bench/kiebitz_bench_ui

#include "board/BoardView.hpp"
#include "kiebitz/core/game.hpp"
#include "notation/NotationView.hpp"
#include "style/AppStyle.hpp"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <benchmark/benchmark.h>

#include <functional>

namespace {

using kiebitz::app::BoardView;
using namespace kiebitz::core;
namespace core = kiebitz::core;

Square sq(std::string_view name) {
    return Square::parse(name).value();
}

void boardRepaint(benchmark::State& state) {
    const int size = static_cast<int>(state.range(0));
    BoardView board;
    board.resize(size, size);
    const Position position =
        Position::fromFen("r1bqk1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4")
            .value();
    board.setPosition(position);
    board.setDrawings(
        {{sq("f3"), sq("g5"), MarkColor::Green}, {sq("c5"), sq("f2"), MarkColor::Red}},
        {{sq("f7"), MarkColor::Red}});
    QImage image(size * 2, size * 2, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(2.0); // Retina
    for ([[maybe_unused]] const auto _ : state) {
        QPainter painter(&image);
        board.render(&painter);
    }
}

/// A repertoire-sized game: a main line with an alternative and a comment at every move, and
/// the same again inside the alternatives, `depth` levels deep. Depth 1 is about 5,000
/// positions, depth 2 about 50,000 (the spec's large repertoire).
core::Game bigGame(int depth = 1) {
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
                game.setComment(next, "A plan worth knowing in this structure");
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
    grow(core::Game::root(), core::Position(), 200, depth);
    return game;
}

void notationRender(benchmark::State& state) {
    const kiebitz::app::AppStyle style(*qApp);
    kiebitz::app::NotationView view(style);
    view.resize(400, 600);
    const core::Game game = bigGame(static_cast<int>(state.range(0)));
    QImage image(800, 1200, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(2.0);
    for ([[maybe_unused]] const auto _ : state) {
        // Everything until the list is on screen: the text, its full layout (the scroll bar
        // needs the height) and the first paint.
        view.setGame(&game, core::Game::root());
        QPainter painter(&image);
        static_cast<QWidget&>(view).render(&painter);
    }
    state.counters["positions"] = static_cast<double>(game.nodeCount());
}

void notationStep(benchmark::State& state) {
    // Moving the highlight one move, as ← → do: must stay far below one frame.
    const kiebitz::app::AppStyle style(*qApp);
    kiebitz::app::NotationView view(style);
    view.resize(400, 600);
    const core::Game game = bigGame();
    view.setGame(&game, core::Game::root());
    const auto line = game.mainLine();
    std::size_t i = 0;
    for ([[maybe_unused]] const auto _ : state) {
        view.setCurrent(line[i]);
        i = (i + 1) % line.size();
    }
}

} // namespace

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
BENCHMARK(boardRepaint)->Arg(480)->Arg(960)->Unit(benchmark::kMillisecond);
BENCHMARK(notationRender)->Arg(1)->Arg(2)->Unit(benchmark::kMillisecond);
BENCHMARK(notationStep)->Unit(benchmark::kMicrosecond);
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    const QApplication app(argc, argv);
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
