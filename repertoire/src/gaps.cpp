#include "kiebitz/repertoire/gaps.hpp"

#include <algorithm>
#include <unordered_set>

namespace kiebitz::repertoire {
namespace {

// Lives only inside findGaps(), so references to its arguments are fine.
// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
struct Walk {
    const Tree& tree;
    const Explore& explore;
    const GapOptions& options;
    const std::function<bool()>& cancelled;
    // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
    GapReport report;
    std::unordered_set<std::uint64_t> seen; ///< positions done (transpositions once)
    std::vector<core::Move> line;

    void visit(const core::Position& position, double likelihood) {
        if (!seen.insert(position.hash()).second) {
            return;
        }
        const Entry* entry = tree.find(position.hash());
        if (entry == nullptr || entry->moves.empty()) {
            return; // the line ends here
        }
        if (tree.isOurMove(*entry)) {
            // Our main answers (alternatives in side variations aren't what we play).
            const bool anyMain = std::any_of(entry->moves.begin(), entry->moves.end(),
                                             [](const auto& move) { return move.main; });
            for (const Continuation& move : entry->moves) {
                if (move.main || !anyMain) {
                    step(position, move.move, likelihood);
                }
            }
            return;
        }
        if (report.asked >= options.maxPositions || (cancelled && cancelled())) {
            report.stopped = true;
            return;
        }
        ++report.asked;
        const std::vector<MoveCount> played = explore(line);
        std::uint64_t total = 0;
        for (const MoveCount& count : played) {
            total += count.games;
        }
        if (total < options.minGames) {
            return; // too few games to say what is likely
        }
        // Their moves, most played first: the answered ones are followed, the others are gaps.
        std::vector<MoveCount> sorted = played;
        std::sort(sorted.begin(), sorted.end(),
                  [](const MoveCount& a, const MoveCount& b) { return a.games > b.games; });
        for (const MoveCount& count : sorted) {
            const double share = static_cast<double>(count.games) / static_cast<double>(total);
            const double reach = likelihood * share;
            if (reach < options.minLikelihood) {
                break; // the rest are rarer still
            }
            const bool answered =
                std::any_of(entry->moves.begin(), entry->moves.end(),
                            [&](const Continuation& move) { return move.move == count.move; });
            if (answered) {
                step(position, count.move, reach);
            } else if (share >= options.minShare) {
                report.gaps.push_back(
                    {line, count.move, position.san(count.move), count.games, share, reach});
            }
        }
    }

    void step(const core::Position& position, core::Move move, double likelihood) {
        core::Position next = position;
        next.play(move);
        line.push_back(move);
        visit(next, likelihood);
        line.pop_back();
    }
};

} // namespace

GapReport findGaps(const Tree& tree, const Explore& explore, const GapOptions& options,
                   const std::function<bool()>& cancelled) {
    Walk walk{tree, explore, options, cancelled, {}, {}, {}};
    walk.visit(core::Position(), 1.0);
    auto& gaps = walk.report.gaps;
    std::stable_sort(gaps.begin(), gaps.end(),
                     [](const Gap& a, const Gap& b) { return a.likelihood > b.likelihood; });
    if (gaps.size() > options.maxGaps) {
        gaps.resize(options.maxGaps);
    }
    return std::move(walk.report);
}

} // namespace kiebitz::repertoire
