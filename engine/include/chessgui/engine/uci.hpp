#pragma once

#include "chessgui/core/position.hpp"
#include "chessgui/engine/score.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// The UCI protocol: what a chess engine prints, read into plain structs, and the commands sent
/// to it. https://backscattering.de/chess/uci/
namespace chessgui::engine {

/// One "info" line with a score and a line (principal variation) to go with it.
struct Info {
    int multiPv = 1; ///< which line, 1 = the best
    int depth = 0;
    int selDepth = 0;
    Score score;                 ///< for the side to move
    std::uint64_t nodes = 0;     ///< 0 when not reported
    std::uint64_t nps = 0;       ///< nodes per second; 0 when not reported
    std::uint64_t timeMs = 0;    ///< 0 when not reported
    std::vector<std::string> pv; ///< moves in UCI notation
};

/// Reads an "info" line. Only lines with a score and at least one move count; progress lines
/// ("info depth 20 currmove e2e4"), strings and anything that isn't an info line give nullopt.
[[nodiscard]] std::optional<Info> parseInfo(std::string_view line);

/// An engine setting it announces ("option name Threads type spin default 1 min 1 max 1024").
struct Option {
    std::string name;
    std::string type; ///< check, spin, combo, button or string
    std::string defaultValue;
    std::optional<int> min;
    std::optional<int> max;
    std::vector<std::string> choices; ///< for combo
};
[[nodiscard]] std::optional<Option> parseOption(std::string_view line);

/// "id name Stockfish 19" → "Stockfish 19".
[[nodiscard]] std::optional<std::string> parseIdName(std::string_view line);

/// "bestmove e2e4 ponder e7e5" (or "bestmove (none)") ends a search.
[[nodiscard]] bool isBestMove(std::string_view line);

/// "setoption name Hash value 256".
[[nodiscard]] std::string setOptionCommand(std::string_view name, std::string_view value);
/// "position fen <fen>".
[[nodiscard]] std::string positionCommand(const core::Position& position);

/// A line of UCI moves as legal moves from `position`, stopping at the first one that isn't legal
/// (an engine's line can go stale when the position changes under it).
[[nodiscard]] std::vector<core::Move> pvMoves(const core::Position& position,
                                              const std::vector<std::string>& pv);

} // namespace chessgui::engine
