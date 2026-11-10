// How long a full board repaint takes (spec: move input to redraw < 16 ms, one frame at 60 Hz).
// Paints offscreen into an image, the same drawing work the screen does.
// Run from the release preset:  ./build/release/bench/chessgui_bench_ui

#include "board/BoardView.hpp"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <benchmark/benchmark.h>

namespace {

using chessgui::app::BoardView;
using namespace chessgui::core;

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

} // namespace

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
BENCHMARK(boardRepaint)->Arg(480)->Arg(960)->Unit(benchmark::kMillisecond);

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
