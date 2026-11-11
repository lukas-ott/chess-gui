# Kiebitz

A fast, minimal, native macOS chess app for opening preparation and training, written in C++20
with Qt 6 Widgets.
It opens instantly, keeps your PGN files as the source of truth, and focuses on building
and training opening repertoires.

> Status: **M6, repertoires; M7, practice.** A sidebar switches between the board (games in
> tabs), the Library and the Engines page. The Library shows every PGN file in your library folder
> (`~/Documents/Kiebitz` by default) as a tree, searchable and filterable by type (game,
> repertoire, tournament, puzzle, other). A file shows its games and a preview; a repertoire file
> an overview (positions, lines, openings) with a Practice button. On the board you can edit
> variations, comments and NAGs (as flowing text or in rows), and changes save themselves. A
> repertoire file opens from its side; its Repertoire tab counts the positions to practise and
> starts a drill in which the app plays the opponent. Mistakes are remembered per position (on
> this Mac, not in the file) and can be practised on their own; `[%depth n]` in a move's comment
> ends the lines below it at move n. Other games are checked against the repertoire of the side
> you view the board from. The Databases page holds game databases of millions of games
> (Lumbra's Gigabase, Caissabase or your own PGN, also in .7z/.zip), and the Explorer tab shows
> what is played in a position, how it scores and both sides' average rating. The Engine tab
> analyses with Stockfish (one-click download) or the Lichess cloud.

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
          again, and file types. No Qt.
repertoire/ Repertoire trees (games merged by position), practice drills and opening names.
          No Qt.
gamedb/   Game databases of millions of games: a compact store (one byte per move), a
          streaming parallel PGN import and an opening explorer over it. No Qt.
app/      Qt Widgets application: main window, board/ (board widget, themes, piece sets),
          library/ (library folder scan and Library tab), databases/ (Databases page,
          imports), explorer/ (Explorer tab), document/ (open games, background
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

Planned modules (`gamedb/`, `online/`) are added
with their milestones.

## Roadmap

- **v1 (MVP):** board, PGN viewer/editor, UCI engine analysis
- **v2:** repertoire builder, depth-limited subtree training, Lichess explorer gap check
- **v3:** Lichess/Chess.com import, compare with my games, large local game database

## Why "Kiebitz"?

In German, a *Kiebitz* is the onlooker who hangs over a game and comments on every move. It's
also the word for the lapwing, the crested bird on the app icon.

## License

GPL-3.0. See [LICENSE](LICENSE).
