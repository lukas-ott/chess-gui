#pragma once

#include "chessgui/core/annotations.hpp"
#include "chessgui/core/position.hpp"
#include "chessgui/core/result.hpp"
#include "chessgui/core/types.hpp"

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chessgui::core {

/// Identifies a node in a Game. Stays valid until that node is removed; after that the id may be
/// reused by a new node.
using NodeId = std::uint32_t;
inline constexpr NodeId kNoNode = std::numeric_limits<NodeId>::max();

/// One position in the game tree: the move that led to it plus its annotations.
/// The root node has no move; it stands for the starting position.
struct Node {
    Move move;
    std::string san; ///< the move in SAN, stored so notation can be shown without replaying

    std::string commentBefore; ///< text shown before the move (start of a variation)
    std::string comment;       ///< text after the move, without [%cal]/[%csl]
    std::vector<std::uint8_t> nags;
    std::vector<Arrow> arrows;
    std::vector<SquareMark> squareMarks;

private:
    friend class Game;
    NodeId m_parent = kNoNode;
    NodeId m_firstChild = kNoNode; ///< the main continuation
    NodeId m_nextSibling = kNoNode;
    bool m_alive = true;
};

/// A chess game as a tree of moves: the main line plus any number of nested variations, each
/// move with optional comments, NAGs, arrows and square marks. Also holds the PGN headers.
///
/// Siblings are ordered: the first child of a node is its main continuation, the others are
/// variations in order. Nodes are stored in one array and linked by index, so the tree is cheap
/// to build and copy.
class Game {
public:
    /// A game from the standard starting position.
    Game();
    /// A game from any position (set up from a FEN).
    explicit Game(Position start);

    // ---- Headers (PGN tag pairs, in file order) ------------------------------------------------
    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& headers() const noexcept {
        return m_headers;
    }
    /// The value of a header, or "" if it isn't set.
    [[nodiscard]] std::string_view header(std::string_view name) const noexcept;
    void setHeader(std::string_view name, std::string_view value);
    void removeHeader(std::string_view name);

    // ---- Reading the tree ----------------------------------------------------------------------
    [[nodiscard]] const Position& startPosition() const noexcept { return m_start; }
    [[nodiscard]] static constexpr NodeId root() noexcept { return 0; }
    [[nodiscard]] const Node& node(NodeId id) const;
    [[nodiscard]] NodeId parent(NodeId id) const;
    /// The main continuation, or kNoNode at the end of a line.
    [[nodiscard]] NodeId mainChild(NodeId id) const;
    /// All continuations, main one first.
    [[nodiscard]] std::vector<NodeId> children(NodeId id) const;
    /// The next variation at the same point, or kNoNode.
    [[nodiscard]] NodeId nextSibling(NodeId id) const;
    /// True if the node is on the game's main line (root included).
    [[nodiscard]] bool isMainLine(NodeId id) const;
    /// Number of moves from the start (root = 0).
    [[nodiscard]] int ply(NodeId id) const;
    /// The move number printed before this node's move ("12" in "12. Nf3" or "12... Nf6").
    [[nodiscard]] int moveNumber(NodeId id) const;
    /// The side that played this node's move.
    [[nodiscard]] Color mover(NodeId id) const;
    /// The nodes from the root to this one, both included.
    [[nodiscard]] std::vector<NodeId> path(NodeId id) const;
    /// The main line from the root, root included.
    [[nodiscard]] std::vector<NodeId> mainLine() const;
    /// The position after this node's move. Replays the moves from the start.
    [[nodiscard]] Position positionAt(NodeId id) const;
    /// Number of nodes in the tree, root included.
    [[nodiscard]] std::size_t nodeCount() const noexcept { return m_nodes.size() - m_free.size(); }

    // ---- Changing the tree ---------------------------------------------------------------------
    /// Adds a move after `parent` and returns its node. The move must be legal. If the same move
    /// is already there, nothing is added and the existing node is returned.
    NodeId addMove(NodeId parent, Move move);
    /// Same as addMove() when the caller already has the position at `parent` (saves a replay).
    NodeId addMove(NodeId parent, const Position& parentPosition, Move move);
    /// Adds a move given in SAN.
    [[nodiscard]] Result<NodeId> addSan(NodeId parent, std::string_view san);
    /// Adds several SAN moves one after another; returns the last node.
    [[nodiscard]] Result<NodeId> addLine(NodeId parent, std::span<const std::string_view> sans);

    /// Removes a node with everything after it ("delete from here"). The root can't be removed.
    void remove(NodeId id);
    /// Removes the whole variation the node is in, from where it branches off. On the main line
    /// it removes from the node onwards.
    void removeVariation(NodeId id);
    /// Moves a variation one place up among its siblings (the first one becomes the main line).
    void promote(NodeId id);
    /// Makes the node's line the main line all the way back to the root.
    void makeMainLine(NodeId id);

    /// Annotations can be edited freely; the move and the links can't.
    void setComment(NodeId id, std::string text);
    void setCommentBefore(NodeId id, std::string text);
    void setNags(NodeId id, std::vector<std::uint8_t> nags);
    void setArrows(NodeId id, std::vector<Arrow> arrows);
    void setSquareMarks(NodeId id, std::vector<SquareMark> marks);

private:
    [[nodiscard]] Node& at(NodeId id);
    [[nodiscard]] const Node& at(NodeId id) const;
    NodeId newNode();
    void unlink(NodeId id);
    void moveToFront(NodeId id);

    Position m_start;
    std::vector<std::pair<std::string, std::string>> m_headers;
    std::vector<Node> m_nodes;  // m_nodes[0] is the root
    std::vector<NodeId> m_free; // ids of removed nodes, reused by newNode()
};

} // namespace chessgui::core
