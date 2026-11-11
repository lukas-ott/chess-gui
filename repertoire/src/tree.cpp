#include "kiebitz/repertoire/tree.hpp"

#include <algorithm>
#include <unordered_set>

namespace kiebitz::repertoire {
namespace {

std::size_t mainMoves(const Entry& entry) {
    return static_cast<std::size_t>(std::count_if(entry.moves.begin(), entry.moves.end(),
                                                  [](const Continuation& c) { return c.main; }));
}

} // namespace

Tree::Tree(core::Color side) : m_side(side) {}

void Tree::add(const core::Game& game, int file, int index) {
    // Depth-first over the game tree, carrying the position, so no move is replayed twice.
    struct Step {
        core::NodeId node;
        core::Position position;
        int ply;
    };
    std::vector<Step> stack{{core::Game::root(), game.startPosition(), 0}};
    while (!stack.empty()) {
        const Step step = std::move(stack.back());
        stack.pop_back();
        const std::uint64_t key = step.position.hash();
        auto [it, isNew] = m_entries.try_emplace(key);
        Entry& entry = it->second;
        if (isNew) {
            entry.toMove = step.position.sideToMove();
            entry.ply = step.ply;
        } else {
            entry.ply = std::min(entry.ply, step.ply);
        }
        entry.sources.push_back({file, index, step.node});

        const std::vector<core::NodeId> children = game.children(step.node);
        std::vector<Step> next;
        next.reserve(children.size());
        bool first = true;
        for (const core::NodeId child : children) { // main line first, so it is listed first
            const core::Node& node = game.node(child);
            core::Position after = step.position;
            after.play(node.move);
            const std::uint64_t to = after.hash();
            Entry& here = m_entries[key]; // the map may have grown: look it up again
            const auto known =
                std::find_if(here.moves.begin(), here.moves.end(),
                             [&](const Continuation& c) { return c.move == node.move; });
            if (known == here.moves.end()) {
                here.moves.push_back({node.move, node.san, to, node.comment, first});
            } else {
                known->main = known->main || first;
                if (known->comment.empty()) {
                    known->comment = node.comment;
                }
            }
            first = false;
            next.push_back({child, std::move(after), step.ply + 1});
        }
        // Walked depth-first, main line first.
        std::reverse(next.begin(), next.end());
        for (Step& later : next) {
            stack.push_back(std::move(later));
        }
    }
}

const Entry* Tree::find(std::uint64_t position) const {
    const auto it = m_entries.find(position);
    return it == m_entries.end() ? nullptr : &it->second;
}

Book Tree::follow(const core::Position& start, std::span<const core::Move> line) const {
    Book book;
    core::Position position = start;
    for (const core::Move& move : line) {
        const Entry* entry = find(position.hash());
        if (entry == nullptr) {
            book.left = true;
            break;
        }
        const auto match = std::find_if(entry->moves.begin(), entry->moves.end(),
                                        [&](const Continuation& c) { return c.move == move; });
        if (match == entry->moves.end()) {
            book.left = true;
            book.byUs = isOurMove(*entry);
            book.expected = entry->moves;
            break;
        }
        position.play(move);
        ++book.plies;
    }
    return book;
}

std::size_t Tree::reach(std::uint64_t position) const {
    std::unordered_set<std::uint64_t> seen{position};
    std::vector<std::uint64_t> stack{position};
    while (!stack.empty()) {
        const Entry* entry = find(stack.back());
        stack.pop_back();
        if (entry == nullptr) {
            continue;
        }
        for (const Continuation& c : entry->moves) {
            if (seen.insert(c.to).second) {
                stack.push_back(c.to);
            }
        }
    }
    return seen.size() - 1;
}

Stats Tree::stats() const {
    Stats stats;
    stats.positions = m_entries.size();
    int deepest = 0;
    for (const auto& [key, entry] : m_entries) {
        (void)key;
        deepest = std::max(deepest, entry.ply);
        if (!isOurMove(entry)) {
            continue;
        }
        ++stats.ourMoves;
        if (mainMoves(entry) > 1) {
            ++stats.conflicts;
        } else if (entry.moves.empty() && entry.ply > 0) {
            ++stats.openEnds;
        }
    }
    stats.depth = (deepest + 1) / 2;
    return stats;
}

std::vector<std::uint64_t> Tree::conflicts() const {
    std::vector<std::uint64_t> result;
    for (const auto& [key, entry] : m_entries) {
        if (isOurMove(entry) && mainMoves(entry) > 1) {
            result.push_back(key);
        }
    }
    std::sort(result.begin(), result.end(),
              [this](std::uint64_t a, std::uint64_t b) { return find(a)->ply < find(b)->ply; });
    return result;
}

std::vector<std::uint64_t> Tree::openEnds() const {
    std::vector<std::uint64_t> result;
    for (const auto& [key, entry] : m_entries) {
        if (isOurMove(entry) && entry.moves.empty() && entry.ply > 0) {
            result.push_back(key);
        }
    }
    std::sort(result.begin(), result.end(),
              [this](std::uint64_t a, std::uint64_t b) { return find(a)->ply < find(b)->ply; });
    return result;
}

} // namespace kiebitz::repertoire
