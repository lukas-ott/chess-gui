#include "kiebitz/repertoire/drill.hpp"

#include <algorithm>

namespace kiebitz::repertoire {

namespace {

/// The n of a [%depth n] command in a comment, or 0.
int depthIn(const std::string& comment) {
    const auto at = comment.find("[%depth");
    if (at == std::string::npos) {
        return 0;
    }
    int value = 0;
    for (std::size_t k = at + 7; k < comment.size(); ++k) {
        const char c = comment[k];
        if (c >= '0' && c <= '9') {
            value = (value * 10) + (c - '0');
        } else if (c != ' ' || value > 0) {
            break;
        }
    }
    return value;
}

} // namespace

Drill::Drill(const core::Game& game, core::Color side, core::NodeId start, int maxDepth,
             std::uint32_t seed, const std::unordered_set<std::uint64_t>* focus)
    : m_game(game), m_side(side), m_start(start), m_maxPly(maxDepth > 0 ? maxDepth * 2 : 0),
      m_random(seed), m_current(start) {
    // A [%depth n] on the way to the start counts too.
    for (const core::NodeId node : game.path(start)) {
        if (const int depth = depthIn(game.node(node).comment); depth > 0) {
            m_limits[start] = depth * 2;
        }
    }
    // The lines (leaves) and the positions you'll be asked about, walking what the drill follows.
    // `focused`: the way here passed a focus position (or there is no focus).
    struct Step {
        core::NodeId node;
        core::Position position;
        bool focused;
    };
    // Positions are only needed to find the focus ones; without a focus none are made.
    std::vector<Step> stack{
        {start, focus != nullptr ? game.positionAt(start) : core::Position(), focus == nullptr}};
    while (!stack.empty()) {
        const Step step = std::move(stack.back());
        stack.pop_back();
        const std::vector<core::NodeId> children = next(step.node);
        if (children.empty()) {
            if (step.node != start && step.focused) {
                m_leaves.insert(step.node);
            }
            continue;
        }
        bool focused = step.focused;
        if (ours(step.node)) {
            const bool asked = focus == nullptr || focus->contains(step.position.hash());
            if (asked) {
                ++m_trainable;
                focused = true;
            }
        }
        const int limit = limitAt(step.node);
        for (const core::NodeId child : children) {
            // A [%depth n] on a move: the lines below it stop at move n.
            if (const int depth = depthIn(game.node(child).comment); depth > 0) {
                m_limits[child] = depth * 2;
            } else if (limit != m_maxPly) {
                m_limits[child] = limit;
            }
            core::Position position;
            if (focus != nullptr) {
                position = step.position;
                position.play(game.node(child).move);
            }
            stack.push_back({child, std::move(position), focused});
        }
    }
}

int Drill::limitAt(core::NodeId node) const {
    const auto it = m_limits.find(node);
    return it != m_limits.end() ? it->second : m_maxPly;
}

bool Drill::ours(core::NodeId node) const {
    // The side to move at a node: the other side than the one that played its move.
    if (node == core::Game::root()) {
        return m_game.startPosition().sideToMove() == m_side;
    }
    return m_game.mover(node) != m_side; // the other side played the move that led here
}

std::vector<core::NodeId> Drill::next(core::NodeId node) const {
    if (const int limit = limitAt(node); limit > 0 && m_game.ply(node) >= limit) {
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
        return m_leaves.contains(node) && !m_done.contains(node);
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
    answer.first = !m_missedHere;
    answer.position = m_game.positionAt(m_current).hash();
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
