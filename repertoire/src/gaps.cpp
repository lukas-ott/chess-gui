#include "kiebitz/repertoire/gaps.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
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

std::vector<core::Move> trunk(const Tree& tree) {
    bool toTheEnd = false;
    std::vector<core::Move> line = sharedLine(tree, &toTheEnd);
    if (toTheEnd) {
        return {}; // nothing branches, so no trunk
    }
    return line;
}

std::vector<core::Move> sharedLine(const Tree& tree, bool* toTheEnd) {
    if (toTheEnd != nullptr) {
        *toTheEnd = false;
    }
    std::vector<core::Move> line;
    core::Position position;
    std::unordered_set<std::uint64_t> seen; // a line that comes back (Nf3 Ng1): stop
    while (seen.insert(position.hash()).second) {
        const Entry* entry = tree.find(position.hash());
        if (entry == nullptr || entry->moves.empty()) {
            if (toTheEnd != nullptr) {
                *toTheEnd = true; // the end of the only line
            }
            return line;
        }
        // Ours: the main moves only (side variations are alternatives); theirs: every answer.
        std::vector<const Continuation*> moves;
        for (const Continuation& move : entry->moves) {
            if (move.main || !tree.isOurMove(*entry)) {
                moves.push_back(&move);
            }
        }
        if (moves.empty() && tree.isOurMove(*entry)) {
            for (const Continuation& move : entry->moves) {
                moves.push_back(&move);
            }
        }
        if (moves.size() != 1) {
            return line; // it branches here
        }
        line.push_back(moves.front()->move);
        position.play(moves.front()->move);
    }
    return line;
}

std::string lineText(std::span<const core::Move> line) {
    std::string text;
    core::Position position;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (!text.empty()) {
            text += ' ';
        }
        if (i % 2 == 0) {
            text += std::to_string((i / 2) + 1) + ". ";
        }
        text += position.san(line[i]);
        position.play(line[i]);
    }
    return text;
}

std::optional<std::vector<core::Move>> parseLine(std::string_view text) {
    std::vector<core::Move> line;
    core::Position position;
    std::istringstream words{std::string(text)};
    std::string word;
    while (words >> word) {
        // "1." or "1..." or "1.e4": the move number goes.
        std::size_t digits = 0;
        while (digits < word.size() &&
               std::isdigit(static_cast<unsigned char>(word[digits])) != 0) {
            ++digits;
        }
        if (digits > 0 && digits < word.size() && word[digits] == '.') {
            word.erase(0, word.find_first_not_of('.', digits));
            if (word.find_first_not_of('.') == std::string::npos) {
                continue;
            }
        }
        const auto move = position.parseSan(word);
        if (!move) {
            return std::nullopt;
        }
        line.push_back(*move);
        position.play(*move);
    }
    return line;
}

GapReport findGaps(const Tree& tree, const Explore& explore, const GapOptions& options,
                   const std::function<bool()>& cancelled) {
    Walk walk{tree, explore, options, cancelled, {}, {}, {}};
    core::Position position;
    for (const core::Move move : options.start) {
        position.play(move);
        walk.line.push_back(move);
    }
    walk.visit(position, 1.0);
    auto& gaps = walk.report.gaps;
    std::stable_sort(gaps.begin(), gaps.end(),
                     [](const Gap& a, const Gap& b) { return a.likelihood > b.likelihood; });
    if (gaps.size() > options.maxGaps) {
        gaps.resize(options.maxGaps);
    }
    return std::move(walk.report);
}

} // namespace kiebitz::repertoire
