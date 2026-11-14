# Kiebitz

A fast, minimal, native macOS chess app for opening preparation and training, written in C++20
with Qt 6 Widgets.
It opens instantly, keeps your PGN files as the source of truth, and focuses on building
and training opening repertoires.

Latest release: **[0.3.0](https://github.com/lukas-ott/kiebitz/releases/tag/v0.3.0)** (pre-release,
Apple silicon). In English, German and Norwegian.

## What it does

- **Board and PGN editor:** games in tabs; variations, comments, NAGs, arrows and clocks, as
  flowing text or in rows. Changes save themselves.
- **Library:** every PGN file in your library folder (`~/Documents/Kiebitz` by default) as a tree,
  searchable (players, events, openings) and filterable by type: game, repertoire (also by
  colour), tournament, puzzle, other. A file shows its games with a preview; every list sorts by any column and
  filters by date.
- **Repertoires:** a repertoire file opens from its side and has a starting position (right-click
  a move). Practice the whole file, from any position, just the positions you got wrong, or
  every file of a side together, with the app playing the opponent. `[%depth n]` in a comment
  ends the lines below it at move n. Other games are checked against your repertoire as you go
  through them.
- **Gap check and theory check:** the common opponent moves a repertoire file doesn't answer yet,
  judged by the Explorer's games; and, on a profile, where its games left your repertoire.
- **Training:** a plan of minutes for openings, middlegame, tactics, endgames and self-study,
  run in order: each part opens its page (your repertoire files, or Lichess puzzles by theme),
  the time shows on every page, and macOS tells you when a part is over. Today, your streak and
  a GitHub-like grid of the days you trained.
- **Puzzles:** the Lichess puzzle database (over 6 million puzzles, one download) by rating and
  theme, starting around your own rating, or the puzzle files of your Library. Hints, the
  solution, and stats for solved, clean, streak and time.
- **Game databases and Explorer:** millions of games (Lumbra's Gigabase, Caissabase or your own
  PGN, also .7z/.zip); the Explorer shows what's played, how it scores and both sides' average
  rating, filtered by rating, years, time control and players (each source keeps its own
  filter). Signed in to Lichess, it also shows Lichess's and the masters' games. With 7 pieces
  or fewer, it shows Lichess's endgame tablebase: who wins, and every move's result, DTZ and DTM.
- **Profiles:** you, friends and opponents, each with their Lichess and chess.com games and
  over-the-board PGNs as a database of their own, with ratings, results by colour and their
  FIDE player.
- **Engine:** Stockfish (one-click download) or the Lichess cloud.
- **Settings:** board, pieces, moves, appearance, language (English, Deutsch, Norsk), sounds
  and your own keyboard shortcuts.

## Requirements (macOS)

```bash
xcode-select --install                     # or full Xcode (adds Instruments for profiling)
brew install cmake ninja ccache qt llvm    # llvm: clang-format/-tidy, clangd, debug-preset compiler
```

All other dependencies (chess-library, SQLite, zstd, GoogleTest, Google Benchmark) are fetched by CMake
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
sql/      Small SQLite helpers the modules below share. No Qt.
library/  The library's cache of game indexes (SQLite), so unchanged files are never read
          again, and file types. No Qt.
repertoire/ Repertoire trees (games merged by position), practice drills and opening names.
          No Qt.
gamedb/   Game databases of millions of games: a compact store (one byte per move), a
          streaming parallel PGN import and an opening explorer over it. No Qt.
puzzles/  Tactics puzzles: solving one, the Lichess puzzle database (imported into one
          compact file, picked by rating and theme) and your puzzle history. No Qt.
training/ The training plan run in order, and the minutes trained per day. No Qt.
app/      Qt Widgets application: main window, board/ (board widget, themes, piece sets),
          library/ (library folder scan and Library tab), repertoire/ (repertoire files,
          practice, the Repertoire tab), puzzles/ (Puzzles page, the Lichess download),
          databases/ (Databases page, imports), explorer/ (Explorer tab), profiles/ (Profiles
          page, game downloads, the Lichess login), settings/ (Settings page, shortcuts),
          training/ (Training page, the activity grid), net/ (talking to Lichess),
          notify/ (macOS notifications), translations/ (German and Norwegian),
          document/ (open games, background file loading and saving), notation/ (move list
          editor), panels/ (game info),
          engine/ (engine processes, Lichess cloud, Stockfish download, Engines page and
          Engine tab),
          style/ (app colours and style sheet, light and dark),
          resources/pieces/ (bundled SVG piece sets, with their licences).
tests/    GoogleTest unit tests; tests/ui/ drives the board and window offscreen.
bench/    Google Benchmark micro-benchmarks.
scripts/  Dev scripts (startup timing).
cmake/    Compiler options and pinned dependencies.
```

## Roadmap

- **v1:** board, PGN viewer and editor, UCI engine analysis (done)
- **v2:** repertoires, practice, game databases, Explorer, profiles, gap check, puzzles (done)
- **0.3:** the endgame tablebase in the Explorer, the training plan, German and Norwegian (done)
- **Next:** game review (your own mistakes as puzzles), position search in big databases

## Why "Kiebitz"?

In German, a *Kiebitz* is the onlooker who hangs over a game and comments on every move. It's
also the word for the lapwing, the crested bird on the app icon.

## License

GPL-3.0. See [LICENSE](LICENSE). The board's sounds are Lichess's sound sets (see
[app/resources/sounds/LICENSE.md](app/resources/sounds/LICENSE.md) for who made them and their
licences); the piece sets keep their own licences (in app/resources/pieces/).
