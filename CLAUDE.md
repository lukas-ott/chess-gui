# CLAUDE.md

Project context for Claude sessions working in this repo.

## What this is

**Kiebitz** (formerly ChessGui): a native macOS chess app (C++20, Qt 6 Widgets, GPL-3.0) for
opening preparation and training.
Main goal: **speed**. App launch, opening files and opening databases all under 1 s.
Spec: https://claude.ai/artifact/72UYJmxrxkknmn2oTY4o3C

## Build commands

```bash
cmake --workflow --preset dev                    # configure + build + test
cmake --preset core-only && cmake --build --preset core-only && ctest --preset core-only
cmake --preset debug && cmake --build --preset debug && ctest --preset debug   # ASan/UBSan, Homebrew clang
./build/release/bench/kiebitz_bench
bash scripts/measure_startup.sh build/release
bash scripts/lint.sh [build-dir]                 # clang-format + clang-tidy, must pass in CI
bash scripts/package_macos.sh                    # self-contained .app + .dmg
```

## Rules

- `core/`, `pgn/`, `engine/`, `library/` (and later `repertoire/`, `gamedb/`) must **never** include Qt.
  Only `app/` (UI) and thin adapters may use Qt. The `ci-core` job builds core without Qt
  to enforce this.
- No blocking work on the UI thread: parsing, I/O, search, network and engine I/O run in the
  background and report back through signals.
- Nothing slow at startup. Load lazily; engines start on demand.
- Every new chess-logic feature gets unit tests. Move generation is verified with perft.
- Engine UI tests use `tests/fake_engine.cpp`, a predictable UCI engine; a `RealEngine` test runs
  Homebrew's Stockfish when it is installed (skipped in CI). Tests never use the network.
- UI behaviour gets tests in `tests/ui/` (real mouse events, offscreen). Set
  `KIEBITZ_SNAPSHOT_DIR=<dir>` when running `kiebitz_ui_tests` to save PNGs of the board.
- Performance-sensitive code gets a benchmark in `bench/`.
- New Qt plugins the app needs (e.g. image formats) go into the `PLUGINS` list in
  `scripts/package_macos.sh`, or the packaged app won't have them.
- Warnings are errors in CI (`-Wall -Wextra -Wpedantic -Wshadow ...`); keep builds clean.
- Third-party code is pinned in `cmake/Dependencies.cmake`; bump versions deliberately.

## Code style

- `.clang-format` is the source of truth (4 spaces, 100 columns).
- Namespaces `kiebitz::core`, `kiebitz::app`, ...; classes `CamelCase`, functions
  `camelBack`, private members `m_name`, constants `kName`.
- Files: `.hpp` / `.cpp`; public core headers under `core/include/kiebitz/core/`.
- UI colours come from `AppStyle` (app/style/): its palette, style sheet and `colors()`.
  Don't hard-code colours in widgets (the board's square and drawing colours are the exception).
- Prefer value types, `std::span`/`std::string_view` for views, no raw `new` outside
  Qt parent/child ownership.

## Milestones

M0 skeleton (done) → M1 core (game tree, SAN/FEN; done) → M2 board UI (done) → M3 PGN viewer/editor (done) →
M4 engine (= v1; done) → M5 library + cache (done) → M6 repertoire → M7 training → M8 explorer + gap check
(= v2) → M9 game import → M10 large database (= v3).
