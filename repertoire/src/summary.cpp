#include "kiebitz/repertoire/summary.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace kiebitz::repertoire {

Summary summarize(std::span<const core::Game> games, core::Color side, const OpeningNames* names) {
    Summary summary;
    std::map<std::string, std::size_t> byName;
    for (const core::Game& game : games) {
        // Walk what practice follows: your main move, every move of the opponent.
        struct Step {
            core::NodeId node;
            core::Position position;
            const Opening* opening;
        };
        std::vector<Step> stack{{core::Game::root(), game.startPosition(), nullptr}};
        while (!stack.empty()) {
            Step step = std::move(stack.back());
            stack.pop_back();
            if (names != nullptr) {
                if (const Opening* named = names->find(step.position.hash())) {
                    step.opening = named;
                }
            }
            const bool ours = step.position.sideToMove() == side;
            std::vector<core::NodeId> next;
            if (ours) {
                if (const core::NodeId main = game.mainChild(step.node); main != core::kNoNode) {
                    next.push_back(main);
                }
            } else {
                next = game.children(step.node);
            }
            if (next.empty()) {
                if (step.node != core::Game::root()) {
                    ++summary.lines;
                    summary.deepestMove = std::max(summary.deepestMove, game.moveNumber(step.node));
                }
                continue;
            }
            if (ours) {
                ++summary.positions;
                if (step.opening != nullptr) {
                    ++byName[step.opening->name];
                }
            }
            for (const core::NodeId child : next) {
                core::Position position = step.position;
                position.play(game.node(child).move);
                stack.push_back({child, std::move(position), step.opening});
            }
        }
    }
    for (auto& [name, count] : byName) {
        summary.openings.push_back({name, count});
    }
    std::stable_sort(summary.openings.begin(), summary.openings.end(),
                     [](const auto& a, const auto& b) { return a.positions > b.positions; });
    return summary;
}

} // namespace kiebitz::repertoire
