#include "kiebitz/repertoire/theory.hpp"

#include <algorithm>
#include <map>
#include <tuple>

namespace kiebitz::repertoire {

TheoryReport checkTheory(const Tree& white, const Tree& black, std::span<const CheckedGame> games) {
    TheoryReport report;
    // By (your colour, position, move): the group's index.
    std::map<std::tuple<core::Color, std::uint64_t, std::string>, std::size_t> groups;
    double bookPlies = 0;
    for (std::size_t index = 0; index < games.size(); ++index) {
        const CheckedGame& game = games[index];
        const Tree& tree = game.side == core::Color::White ? white : black;
        if (tree.empty()) {
            continue; // no repertoire for this colour
        }
        ++report.checked;
        const Book book = tree.follow(core::Position(), game.line);
        bookPlies += book.plies;
        const auto at = static_cast<std::size_t>(book.plies);
        if (!book.left || at >= game.line.size()) {
            ++report.stayed;
            continue;
        }
        Departure kind = Departure::End;
        if (!book.expected.empty()) {
            kind = book.byUs ? Departure::You : Departure::Opponent;
        }
        core::Position position;
        for (std::size_t k = 0; k < at; ++k) {
            position.play(game.line[k]);
        }
        const core::Move played = game.line[at];
        // A repertoire's end is one place, whatever is played next.
        const std::string key = kind == Departure::End ? std::string() : played.uci();
        const auto [found, added] =
            groups.try_emplace({game.side, position.hash(), key}, report.groups.size());
        if (added) {
            DepartureGroup group;
            group.kind = kind;
            group.line.assign(game.line.begin(),
                              game.line.begin() + static_cast<std::ptrdiff_t>(at));
            group.played = played;
            group.san = position.san(played);
            group.side = game.side;
            // Ours: the main moves (alternatives aside); theirs: every move answered.
            const bool anyMain =
                kind == Departure::You && std::any_of(book.expected.begin(), book.expected.end(),
                                                      [](const auto& move) { return move.main; });
            for (const Continuation& move : book.expected) {
                if (move.main || !anyMain) {
                    group.expected.push_back(move.san);
                }
            }
            report.groups.push_back(std::move(group));
        }
        report.groups[found->second].games.push_back(index);
    }
    if (report.checked > 0) {
        report.averageBookMoves = bookPlies / static_cast<double>(report.checked) / 2.0;
    }
    std::stable_sort(report.groups.begin(), report.groups.end(),
                     [](const DepartureGroup& a, const DepartureGroup& b) {
                         return a.games.size() > b.games.size();
                     });
    return report;
}

} // namespace kiebitz::repertoire
