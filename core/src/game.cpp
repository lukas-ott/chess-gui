#include "kiebitz/core/game.hpp"

#include <algorithm>
#include <cassert>

namespace kiebitz::core {

Game::Game() : Game(Position()) {}

Game::Game(Position start) : m_start(std::move(start)) {
    m_nodes.emplace_back(); // root
    // The Seven Tag Roster every PGN game starts with.
    m_headers = {{"Event", "?"}, {"Site", "?"},  {"Date", "????.??.??"}, {"Round", "?"},
                 {"White", "?"}, {"Black", "?"}, {"Result", "*"}};
    if (m_start.fen() != kStartFen) {
        m_headers.emplace_back("SetUp", "1");
        m_headers.emplace_back("FEN", m_start.fen());
    }
}

// ---- Headers -----------------------------------------------------------------------------------

std::string_view Game::header(std::string_view name) const noexcept {
    for (const auto& [key, value] : m_headers) {
        if (key == name) {
            return value;
        }
    }
    return {};
}

void Game::setHeader(std::string_view name, std::string_view value) {
    for (auto& [key, existing] : m_headers) {
        if (key == name) {
            existing = value;
            return;
        }
    }
    m_headers.emplace_back(name, value);
}

void Game::removeHeader(std::string_view name) {
    std::erase_if(m_headers, [&](const auto& entry) { return entry.first == name; });
}

// ---- Reading -----------------------------------------------------------------------------------

Node& Game::at(NodeId id) {
    assert(id < m_nodes.size() && m_nodes[id].m_alive);
    return m_nodes[id];
}

const Node& Game::at(NodeId id) const {
    assert(id < m_nodes.size() && m_nodes[id].m_alive);
    return m_nodes[id];
}

const Node& Game::node(NodeId id) const {
    return at(id);
}

NodeId Game::parent(NodeId id) const {
    return at(id).m_parent;
}

NodeId Game::mainChild(NodeId id) const {
    return at(id).m_firstChild;
}

NodeId Game::nextSibling(NodeId id) const {
    return at(id).m_nextSibling;
}

std::vector<NodeId> Game::children(NodeId id) const {
    std::vector<NodeId> result;
    for (NodeId child = at(id).m_firstChild; child != kNoNode; child = at(child).m_nextSibling) {
        result.push_back(child);
    }
    return result;
}

bool Game::isMainLine(NodeId id) const {
    for (NodeId current = id; current != root(); current = at(current).m_parent) {
        if (at(at(current).m_parent).m_firstChild != current) {
            return false;
        }
    }
    return true;
}

int Game::ply(NodeId id) const {
    int count = 0;
    for (NodeId current = id; current != root(); current = at(current).m_parent) {
        ++count;
    }
    return count;
}

int Game::moveNumber(NodeId id) const {
    const int blackStarts = m_start.sideToMove() == Color::Black ? 1 : 0;
    const int plies = ply(id);
    if (plies == 0) {
        return m_start.fullmoveNumber();
    }
    return m_start.fullmoveNumber() + ((plies - 1 + blackStarts) / 2);
}

Color Game::mover(NodeId id) const {
    const int blackStarts = m_start.sideToMove() == Color::Black ? 1 : 0;
    return (ply(id) - 1 + blackStarts) % 2 == 0 ? Color::White : Color::Black;
}

std::vector<NodeId> Game::path(NodeId id) const {
    std::vector<NodeId> result(static_cast<std::size_t>(ply(id)) + 1);
    NodeId current = id;
    for (std::size_t i = result.size(); i-- > 0;) { // fill from the end: node, parent, ...
        result[i] = current;
        current = at(current).m_parent;
    }
    return result;
}

std::vector<NodeId> Game::mainLine() const {
    std::vector<NodeId> result;
    for (NodeId current = root(); current != kNoNode; current = at(current).m_firstChild) {
        result.push_back(current);
    }
    return result;
}

Position Game::positionAt(NodeId id) const {
    Position position = m_start;
    const auto nodes = path(id);
    for (std::size_t i = 1; i < nodes.size(); ++i) {
        position.play(at(nodes[i]).move);
    }
    return position;
}

// ---- Changing ----------------------------------------------------------------------------------

NodeId Game::newNode() {
    if (!m_free.empty()) {
        const NodeId id = m_free.back();
        m_free.pop_back();
        m_nodes[id] = Node{};
        return id;
    }
    m_nodes.emplace_back();
    return static_cast<NodeId>(m_nodes.size() - 1);
}

NodeId Game::addMove(NodeId parent, Move move) {
    return addMove(parent, positionAt(parent), move);
}

NodeId Game::addMove(NodeId parent, const Position& parentPosition, Move move) {
    assert(parentPosition.isLegal(move));
    // Same move already there? Then just go to it, as every chess GUI does.
    NodeId last = kNoNode;
    for (NodeId child = at(parent).m_firstChild; child != kNoNode;
         child = at(child).m_nextSibling) {
        if (at(child).move == move) {
            return child;
        }
        last = child;
    }

    const NodeId id = newNode(); // may reallocate m_nodes: take references only after this
    Node& node = m_nodes[id];
    node.move = move;
    node.san = parentPosition.san(move);
    node.m_parent = parent;
    if (last == kNoNode) {
        at(parent).m_firstChild = id;
    } else {
        at(last).m_nextSibling = id;
    }
    return id;
}

Result<NodeId> Game::addSan(NodeId parent, std::string_view san) {
    const Position position = positionAt(parent);
    auto move = position.parseSan(san);
    if (!move) {
        return move.error();
    }
    return addMove(parent, position, *move);
}

Result<NodeId> Game::addLine(NodeId parent, std::span<const std::string_view> sans) {
    Position position = positionAt(parent);
    NodeId current = parent;
    for (const auto san : sans) {
        auto move = position.parseSan(san);
        if (!move) {
            const char* const dots = position.sideToMove() == Color::White ? ". " : "... ";
            return Error{std::to_string(position.fullmoveNumber()) + dots + move.error().message};
        }
        current = addMove(current, position, *move);
        position.play(*move);
    }
    return current;
}

void Game::unlink(NodeId id) {
    Node& parentNode = at(at(id).m_parent);
    if (parentNode.m_firstChild == id) {
        parentNode.m_firstChild = at(id).m_nextSibling;
    } else {
        NodeId previous = parentNode.m_firstChild;
        while (at(previous).m_nextSibling != id) {
            previous = at(previous).m_nextSibling;
        }
        at(previous).m_nextSibling = at(id).m_nextSibling;
    }
    at(id).m_nextSibling = kNoNode;
}

void Game::remove(NodeId id) {
    assert(id != root());
    if (id == root()) {
        return;
    }
    unlink(id);
    // Free the whole subtree.
    std::vector<NodeId> pending{id};
    while (!pending.empty()) {
        const NodeId current = pending.back();
        pending.pop_back();
        for (NodeId child = at(current).m_firstChild; child != kNoNode;
             child = at(child).m_nextSibling) {
            pending.push_back(child);
        }
        m_nodes[current] = Node{};
        m_nodes[current].m_alive = false;
        m_free.push_back(current);
    }
}

void Game::removeVariation(NodeId id) {
    // Walk up to the node where this variation branches off (the first that isn't a main child).
    NodeId start = id;
    for (NodeId current = id; current != root(); current = at(current).m_parent) {
        if (at(at(current).m_parent).m_firstChild != current) {
            start = current;
            break;
        }
    }
    remove(start);
}

void Game::moveToFront(NodeId id) {
    const NodeId parentId = at(id).m_parent;
    if (at(parentId).m_firstChild == id) {
        return;
    }
    unlink(id);
    at(id).m_nextSibling = at(parentId).m_firstChild;
    at(parentId).m_firstChild = id;
}

void Game::promote(NodeId id) {
    if (id == root()) {
        return;
    }
    const NodeId parentId = at(id).m_parent;
    const NodeId first = at(parentId).m_firstChild;
    if (first == id) {
        return;
    }
    // Find the sibling before `id` and the one before that, then swap the two.
    NodeId beforePrevious = kNoNode;
    NodeId previous = first;
    while (at(previous).m_nextSibling != id) {
        beforePrevious = previous;
        previous = at(previous).m_nextSibling;
    }
    at(previous).m_nextSibling = at(id).m_nextSibling;
    at(id).m_nextSibling = previous;
    if (beforePrevious == kNoNode) {
        at(parentId).m_firstChild = id;
    } else {
        at(beforePrevious).m_nextSibling = id;
    }
}

void Game::makeMainLine(NodeId id) {
    for (NodeId current = id; current != root(); current = at(current).m_parent) {
        moveToFront(current);
    }
}

void Game::setComment(NodeId id, std::string text) {
    at(id).comment = std::move(text);
}

void Game::setCommentBefore(NodeId id, std::string text) {
    at(id).commentBefore = std::move(text);
}

void Game::setNags(NodeId id, std::vector<std::uint8_t> nags) {
    at(id).nags = std::move(nags);
}

void Game::setArrows(NodeId id, std::vector<Arrow> arrows) {
    at(id).arrows = std::move(arrows);
}

void Game::setSquareMarks(NodeId id, std::vector<SquareMark> marks) {
    at(id).squareMarks = std::move(marks);
}

} // namespace kiebitz::core
