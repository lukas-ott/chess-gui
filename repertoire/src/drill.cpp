#include "kiebitz/repertoire/drill.hpp"

#include <algorithm>

namespace kiebitz::repertoire {

Drill::Drill(const core::Game& game, core::Color side, core::NodeId start, int maxDepth,
             std::uint32_t seed)
    : m_game(game), m_side(side), m_start(start), m_maxPly(maxDepth > 0 ? maxDepth * 2 : 0),
      m_random(seed), m_current(start) {
    // The lines (leaves) and the positions you'll be asked about, walking what the drill follows.
    std::vector<core::NodeId> stack{start};
    while (!stack.empty()) {
        const core::NodeId node = stack.back();
        stack.pop_back();
        const std::vector<core::NodeId> children = next(node);
        if (children.empty()) {
            if (node != start) {
                m_leaves.insert(node);
            }
            continue;
        }
        if (ours(node)) {
            ++m_trainable;
        }
        stack.insert(stack.end(), children.begin(), children.end());
    }
}

bool Drill::ours(core::NodeId node) const {
    // The side to move at a node: the other side than the one that played its move.
    if (node == core::Game::root()) {
        return m_game.startPosition().sideToMove() == m_side;
    }
    return m_game.mover(node) != m_side; // the other side played the move that led here
}

std::vector<core::NodeId> Drill::next(core::NodeId node) const {
    if (m_maxPly > 0 && m_game.ply(node) >= m_maxPly) {
        return {}; // the depth limit: the line ends here
    }
    if (ours(node)) {
        const core::NodeId main = m_game.mainChild(node);
        return main == core::kNoNode ? std::vector<core::NodeId>{}
                                     : std::vector<core::NodeId>{main};
    }
    return m_game.children(node);
}

bool Drill::hasOpenLine(core::NodeId node) const {
    if (isLeaf(node)) {
        return !m_done.contains(node);
    }
    const auto children = next(node);
    return std::any_of(children.begin(), children.end(),
                       [this](core::NodeId child) { return hasOpenLine(child); });
}

bool Drill::yourMove() const {
    return !lineEnded() && ours(m_current);
}

bool Drill::lineEnded() const {
    return isLeaf(m_current);
}

std::optional<core::NodeId> Drill::opponentMove() {
    if (lineEnded() || ours(m_current)) {
        return std::nullopt;
    }
    std::vector<core::NodeId> open;
    for (const core::NodeId child : next(m_current)) {
        if (hasOpenLine(child)) {
            open.push_back(child);
        }
    }
    if (open.empty()) { // everything below is done this round: any reply will do
        open = next(m_current);
    }
    std::uniform_int_distribution<std::size_t> pick(0, open.size() - 1);
    m_current = open[pick(m_random)];
    m_missedHere = false;
    return m_current;
}

Drill::Answer Drill::play(core::Move move) {
    Answer answer;
    if (!yourMove()) {
        return answer;
    }
    const core::NodeId expected = next(m_current).front();
    answer.expected = m_game.node(expected).move;
    if (move == answer.expected) {
        answer.correct = true;
        if (!m_missedHere) {
            ++m_correct;
        }
        m_current = expected;
        m_missedHere = false;
        return answer;
    }
    if (!m_missedHere) { // one mistake per position, however many tries
        ++m_mistakes;
        m_missed.push_back(m_current);
        m_missedHere = true;
    }
    return answer;
}

void Drill::nextLine() {
    if (lineEnded() && m_current != m_start) {
        m_done.insert(m_current);
    }
    m_current = m_start;
    m_missedHere = false;
}

} // namespace kiebitz::repertoire
