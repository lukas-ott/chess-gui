# Kiebitz

A *Kiebitz* is German for the onlooker who watches a game and comments on every move, and for
the lapwing, the crested bird on the icon. (Formerly ChessGui.)

A fast, native macOS chess app for opening preparation and training, written in C++20 with
Qt 6 Widgets.
It opens instantly, keeps your PGN files as the source of truth, and focuses on building
and training opening repertoires.

> Status: **M5, library cache and fast move list.** A sidebar switches between the board (games in tabs),
> the Library and the Engines page. The Library shows every PGN file in your library folder
> (`~/Documents/Kiebitz` by default) with a game list and preview. On the board you can edit
> variations, comments and NAGs, edit headers, save back into the file, and copy or paste PGN
> and FEN. The Engines page downloads the latest Stockfish in one click (or uses the one on your
> Mac, or any UCI engine); the Engine tab analyses the board with it or the Lichess cloud: up to
> 5 lines, an eval bar and best-move arrows, and a click on a move plays the line into the game.
> Space starts and stops it. The Library keeps a cache of every file's game index, so it lists
> even large collections instantly, and the move list stays fast with 50,000-position repertoires.

## Requirements (macOS)

```bash
xcode-select --install                     # or full Xcode (adds Instruments for profiling)
brew install cmake ninja ccache qt llvm    # llvm: clang-format/-tidy, clangd, debug-preset compiler
```

All other dependencies (chess-library, SQLite, GoogleTest, Google Benchmark) are fetched by CMake
at pinned versions on first configure.

## Build and run

```bash
cmake --workflow --preset dev              # configure + build + test
open build/dev/app/Kiebitz.app
```

| Preset      | Use it for                                                       |
| ----------- | ---------------------------------------------------------------- |
| `debug`     | Debugging, with AddressSanitizer + UBSan (built with Homebrew's clang) |
| `dev`       | Daily running of the app (optimised, still debuggable)           |
| `release`   | Speed measurements and benchmarks (LTO)                          |
| `core-only` | Fast loop on chess logic without building Qt code                |
| `ci`, `ci-core` | What GitHub Actions runs                                     |

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --preset release && cmake --build --preset release
./build/release/bench/kiebitz_bench       # move generation speed
bash scripts/measure_startup.sh build/release   # startup time vs. the 1 s target
bash scripts/lint.sh                       # clang-format + clang-tidy (CI runs this too)
```

## Packaging

```bash
bash scripts/package_macos.sh              # → build/release/package/Kiebitz-<version>.dmg
```

This builds the release preset and copies Qt and every library it needs into `Kiebitz.app`,
so the app runs on a Mac without Homebrew or Qt. It needs at least the macOS version of the
Mac that built it, because Homebrew builds Qt for that version (see the top of `CMakeLists.txt`). The script fails if anything
in the bundle still loads from outside it. CI builds the same DMG on every push and attaches it
to the run as a download.

The app is signed ad hoc, not with an Apple Developer ID, so on another Mac the first launch
needs right-click → Open. Proper signing and notarisation need a paid Apple Developer account.

The `debug` preset compiles with Homebrew's clang (`/opt/homebrew/opt/llvm`) because Apple's
AddressSanitizer hangs at startup on macOS 26. All other presets use Apple's compiler.

In CLion or VS Code (CMake Tools + clangd), just open the folder: both pick up the presets.

## Layout

```
core/     Pure C++ chess logic: positions, FEN, SAN, game tree, annotations. No Qt.
pgn/      Reading, indexing and writing PGN files (fast game index, round-trip safe). No Qt.
engine/   The UCI protocol (engine output, scores, lines as moves) and picking the Stockfish
          download for this Mac. No Qt.
library/  The library's cache of game indexes (SQLite), so unchanged files are never read
          again. No Qt.
app/      Qt Widgets application: main window, board/ (board widget, themes, piece sets),
          library/ (library folder scan and Library tab), document/ (open games, background
          file loading and saving), notation/ (move list editor), panels/ (game info),
          engine/ (engine processes, Lichess cloud, Stockfish download, Engines page and
          Engine tab),
          style/ (app colours and style sheet, light and dark),
          resources/pieces/ (bundled SVG piece sets, with their licences).
tests/    GoogleTest unit tests; tests/ui/ drives the board and window offscreen.
bench/    Google Benchmark micro-benchmarks.
scripts/  Dev scripts (startup timing).
cmake/    Compiler options and pinned dependencies.
```

Planned modules (`repertoire/`, `gamedb/`, `online/`) are added
with their milestones.

## Roadmap

- **v1 (MVP):** board, PGN viewer/editor, UCI engine analysis
- **v2:** repertoire builder, depth-limited subtree training, Lichess explorer gap check
- **v3:** Lichess/Chess.com import, compare with my games, large local game database

## License

GPL-3.0. See [LICENSE](LICENSE).
