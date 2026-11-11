#include "kiebitz/repertoire/openings.hpp"

namespace kiebitz::repertoire {
namespace {

/// The next field of a tab-separated line.
std::string_view field(std::string_view& line) {
    const auto tab = line.find('\t');
    const std::string_view result = line.substr(0, tab);
    line.remove_prefix(tab == std::string_view::npos ? line.size() : tab + 1);
    return result;
}

} // namespace

OpeningNames OpeningNames::fromTsv(std::string_view tsv) {
    OpeningNames names;
    names.m_names.reserve(4096);
    while (!tsv.empty()) {
        const auto end = tsv.find('\n');
        std::string_view line = tsv.substr(0, end);
        tsv.remove_prefix(end == std::string_view::npos ? tsv.size() : end + 1);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        const std::string_view eco = field(line);
        const std::string_view name = field(line);
        std::string_view moves = field(line);
        if (eco.size() != 3 || name.empty() || moves.empty()) {
            continue; // the header line, or a broken one
        }
        core::Position position;
        bool ok = true;
        while (ok && !moves.empty()) {
            const auto space = moves.find(' ');
            const std::string_view token = moves.substr(0, space);
            moves.remove_prefix(space == std::string_view::npos ? moves.size() : space + 1);
            if (token.empty() || token.back() == '.') {
                continue; // a move number
            }
            const auto move = position.parseSan(token);
            ok = move.ok();
            if (ok) {
                position.play(*move);
            }
        }
        if (ok) {
            // The most specific name wins where the list names a position twice.
            auto& entry = names.m_names[position.hash()];
            if (entry.name.size() < name.size()) {
                entry = {std::string(eco), std::string(name)};
            }
        }
    }
    return names;
}

const Opening* OpeningNames::find(std::uint64_t position) const {
    const auto it = m_names.find(position);
    return it == m_names.end() ? nullptr : &it->second;
}

const Opening* OpeningNames::along(const core::Position& start,
                                   std::span<const core::Move> line) const {
    core::Position position = start;
    const Opening* last = find(position.hash());
    for (const core::Move& move : line) {
        position.play(move);
        if (const Opening* named = find(position.hash())) {
            last = named;
        }
    }
    return last;
}

} // namespace kiebitz::repertoire
