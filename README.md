# ChessGui

A fast, native macOS chess GUI for opening preparation, written in C++20 with Qt 6 Widgets.
It opens instantly, keeps your PGN files as the source of truth, and focuses on building
and training opening repertoires.

> Status: **M1, chess core.** Positions, FEN, SAN and the game tree with variations and
> annotations work and are tested. The window still shows a placeholder layout; the board UI is M2.

## Requirements (macOS)

```bash
xcode-select --install                     # or full Xcode (adds Instruments for profiling)
brew install cmake ninja ccache qt llvm    # llvm only for clang-format / clang-tidy / clangd
```

All other dependencies (chess-library, GoogleTest, Google Benchmark) are fetched by CMake
at pinned versions on first configure.

## Build and run

```bash
cmake --workflow --preset dev              # configure + build + test
open build/dev/app/ChessGui.app
```

| Preset      | Use it for                                                       |
| ----------- | ---------------------------------------------------------------- |
| `debug`     | Debugging, with AddressSanitizer + UBSan                         |
| `dev`       | Daily running of the app (optimised, still debuggable)           |
| `release`   | Speed measurements and benchmarks (LTO)                          |
| `core-only` | Fast loop on chess logic without building Qt code                |
| `ci`, `ci-core` | What GitHub Actions runs                                     |

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --preset release && cmake --build --preset release
./build/release/bench/chessgui_bench       # move generation speed
bash scripts/measure_startup.sh build/release   # startup time vs. the 1 s target
bash scripts/lint.sh                       # clang-format + clang-tidy (CI runs this too)
```

## Packaging

```bash
bash scripts/package_macos.sh              # → build/release/package/ChessGui-<version>.dmg
```

This builds the release preset and copies Qt and every library it needs into `ChessGui.app`,
so the app runs on a Mac without Homebrew or Qt (macOS 14.4+). The script fails if anything
in the bundle still loads from outside it. CI builds the same DMG on every push and attaches it
to the run as a download.

The app is signed ad hoc, not with an Apple Developer ID, so on another Mac the first launch
needs right-click → Open. Proper signing and notarisation need a paid Apple Developer account.

In CLion or VS Code (CMake Tools + clangd), just open the folder: both pick up the presets.

## Layout

```
core/     Pure C++ chess logic: positions, FEN, SAN, game tree, annotations. No Qt.
app/      Qt Widgets application (main window, board view, panels).
tests/    GoogleTest unit tests (perft-verified move generation, ...).
bench/    Google Benchmark micro-benchmarks.
scripts/  Dev scripts (startup timing).
cmake/    Compiler options and pinned dependencies.
```

Planned modules (`pgn/`, `engine/`, `library/`, `repertoire/`, `gamedb/`, `online/`) are added
with their milestones.

## Roadmap

- **v1 (MVP):** board, PGN viewer/editor, UCI engine analysis
- **v2:** repertoire builder, depth-limited subtree training, Lichess explorer gap check
- **v3:** Lichess/Chess.com import, compare with my games, large local game database

## License

GPL-3.0. See [LICENSE](LICENSE).
