#pragma once

#include "kiebitz/core/game.hpp"

#include <cstdint>
#include <optional>
#include <random>
#include <set>
#include <vector>

namespace kiebitz::repertoire {

/// Practising a repertoire: the app plays the opponent's moves, you play yours.
///
/// The drill walks a repertoire's move tree from a start node. Where it is your move, the
/// expected move is the main continuation (moves only in side variations are alternatives and
/// aren't asked for). Where it is the opponent's move, it picks at random among the replies whose
/// lines you haven't finished yet, so each line comes up once per round. A line ends at the end
/// of the tree or at the depth limit; the drill then starts over from the start node, and it is
/// finished when every line has been played.
class Drill {
public:
    /// `side` is the side you play. `maxDepth` limits the lines to that full move ("12" = up to
    /// and including move 12); 0 means no limit. `seed` makes the opponent's choices repeatable.
    Drill(const core::Game& game, core::Color side, core::NodeId start, int maxDepth,
          std::uint32_t seed);

    /// Positions where you will be asked for a move.
    [[nodiscard]] std::size_t trainable() const noexcept { return m_trainable; }
    /// How many lines there are, and how many are done.
    [[nodiscard]] std::size_t lines() const noexcept { return m_leaves.size(); }
    [[nodiscard]] std::size_t linesDone() const noexcept { return m_done.size(); }
    [[nodiscard]] bool finished() const noexcept { return m_done.size() == m_leaves.size(); }

    /// Where the drill is now (a node of the game).
    [[nodiscard]] core::NodeId current() const noexcept { return m_current; }
    /// It is your move (otherwise the opponent's, or the line has ended).
    [[nodiscard]] bool yourMove() const;
    /// The line has ended at current(): call nextLine() to go on.
    [[nodiscard]] bool lineEnded() const;

    /// The opponent's move: goes there and returns its node (when it is not your move).
    std::optional<core::NodeId> opponentMove();

    struct Answer {
        bool correct = false;
        core::Move expected; ///< the repertoire's move here
    };
    /// Your move. Right: the drill goes on. Wrong: it stays, counts a mistake (once per
    /// position and round) and says what was expected.
    Answer play(core::Move move);

    /// After a line ended: marks it done and goes back to the start.
    void nextLine();

    [[nodiscard]] int correct() const noexcept { return m_correct; }
    [[nodiscard]] int mistakes() const noexcept { return m_mistakes; }
    /// The positions you got wrong (nodes where you were to move), in order.
    [[nodiscard]] const std::vector<core::NodeId>& missed() const noexcept { return m_missed; }

private:
    /// The moves the drill follows from a node: all of the opponent's, your main one.
    [[nodiscard]] std::vector<core::NodeId> next(core::NodeId node) const;
    [[nodiscard]] bool isLeaf(core::NodeId node) const { return next(node).empty(); }
    [[nodiscard]] bool hasOpenLine(core::NodeId node) const;
    [[nodiscard]] bool ours(core::NodeId node) const; ///< you are to move at this node

    const core::Game& m_game;
    core::Color m_side;
    core::NodeId m_start;
    int m_maxPly;
    std::mt19937 m_random;

    std::set<core::NodeId> m_leaves;
    std::set<core::NodeId> m_done;
    std::size_t m_trainable = 0;
    core::NodeId m_current;
    bool m_missedHere = false;
    int m_correct = 0;
    int m_mistakes = 0;
    std::vector<core::NodeId> m_missed;
};

} // namespace kiebitz::repertoire
