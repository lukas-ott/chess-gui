#pragma once

#include "kiebitz/core/game.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiebitz::puzzles {

/// A tactics puzzle: a position and the moves that solve it, as a game tree. The main line is
/// the solution (your moves and the opponent's answers); variations at the opponent's moves are
/// other answers the solution covers (a puzzle file's notes), and are not played.
struct Puzzle {
    std::string id;                  ///< Lichess's id ("00sHx"), or "" for a puzzle from a file
    core::Game game;                 ///< from the puzzle's position
    bool opponentFirst = false;      ///< Lichess: the first move is the opponent's, and sets it up
    int rating = 0;                  ///< 0: unknown
    std::vector<std::string> themes; ///< Lichess's themes ("fork", "mateIn2")
};

/// One line of the Lichess puzzle database (lichess_db_puzzle.csv):
/// PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags.
/// The moves are in UCI, the opponent's first. Nothing for the header or a broken line.
[[nodiscard]] std::optional<Puzzle> parseLichessLine(std::string_view line);

/// A puzzle from a game of a puzzle file: its start position (a FEN) and its moves, the side to
/// move solving. Nothing when it has no moves (a file of tasks without their solutions).
[[nodiscard]] std::optional<Puzzle> fromGame(core::Game game);

/// "backRankMate" → "Back rank mate": a theme as shown.
[[nodiscard]] std::string themeName(std::string_view theme);

/// Solving a puzzle, move by move.
class Solve {
public:
    enum class Verdict : std::uint8_t {
        Right,  ///< the right move: the opponent answers next (reply())
        Wrong,  ///< not the move: nothing is played; try again
        Solved, ///< the last move (or another move that mates)
    };

    /// Starts after the opponent's first move when it sets the puzzle up.
    explicit Solve(const Puzzle& puzzle);

    [[nodiscard]] const core::Game& game() const noexcept { return m_game; }
    /// Where the board is: the last move played (root before any).
    [[nodiscard]] core::NodeId node() const noexcept { return m_node; }
    [[nodiscard]] core::Position position() const { return m_game.positionAt(m_node); }
    /// The side that solves it.
    [[nodiscard]] core::Color solver() const noexcept { return m_solver; }
    /// Where the solving starts (after the opponent's setting-up move).
    [[nodiscard]] core::NodeId start() const noexcept { return m_start; }

    /// Your move, when it's your turn. A move that mates is right even if it isn't the one
    /// the puzzle has (as on Lichess).
    Verdict play(core::Move move);
    /// The opponent's answer after a right move: played, and returned (kNoNode if none).
    core::NodeId reply();
    /// Whether it's the opponent's turn to answer (after a right move).
    [[nodiscard]] bool awaitingReply() const;

    /// The move to find now (for a hint and for showing the solution).
    [[nodiscard]] std::optional<core::Move> expected() const;
    [[nodiscard]] bool solved() const noexcept { return m_solved; }
    /// A wrong move was played on the way (or the solution was shown): failed, even if solved.
    [[nodiscard]] bool failed() const noexcept { return m_failed; }
    void markFailed() noexcept { m_failed = true; }

private:
    core::Game m_game;
    core::NodeId m_node = core::Game::root();
    core::NodeId m_start = core::Game::root();
    core::Color m_solver = core::Color::White;
    bool m_solved = false;
    bool m_failed = false;
};

} // namespace kiebitz::puzzles
