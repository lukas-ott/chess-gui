#include "chessgui/core/annotations.hpp"

#include <array>
#include <initializer_list>
#include <span>

namespace chessgui::core {
namespace {

std::optional<MarkColor> colorFromLetter(char c) {
    switch (c) {
    case 'G':
        return MarkColor::Green;
    case 'R':
        return MarkColor::Red;
    case 'B':
        return MarkColor::Blue;
    case 'Y':
        return MarkColor::Yellow;
    default:
        return std::nullopt;
    }
}

char letterFromColor(MarkColor color) {
    constexpr std::array<char, 4> kLetters{'G', 'R', 'B', 'Y'};
    return kLetters[static_cast<std::size_t>(color)];
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\n' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

/// Calls `entry` for each comma-separated item of "Ge2e4, Rd1d8".
template <typename Fn> void forEachEntry(std::string_view list, Fn entry) {
    while (!list.empty()) {
        const auto comma = list.find(',');
        entry(trim(list.substr(0, comma)));
        if (comma == std::string_view::npos) {
            break;
        }
        list.remove_prefix(comma + 1);
    }
}

} // namespace

CommentParts parseComment(std::string_view comment) {
    CommentParts parts;
    std::string text;
    std::size_t pos = 0;
    while (pos < comment.size()) {
        const auto start = comment.find("[%", pos);
        const auto end = start == std::string_view::npos ? start : comment.find(']', start);
        if (end == std::string_view::npos) {
            text.append(comment.substr(pos));
            break;
        }
        text.append(comment.substr(pos, start - pos));
        const auto command = trim(comment.substr(start + 2, end - start - 2));
        const auto space = command.find_first_of(" \t\r\n"); // a PGN line break may split it
        const auto name = command.substr(0, space);
        const auto args =
            space == std::string_view::npos ? std::string_view{} : trim(command.substr(space + 1));
        if (name == "cal") {
            forEachEntry(args, [&](std::string_view e) {
                const auto color = e.size() == 5 ? colorFromLetter(e[0]) : std::nullopt;
                const auto from = color ? Square::parse(e.substr(1, 2)) : std::nullopt;
                const auto to = color ? Square::parse(e.substr(3, 2)) : std::nullopt;
                if (from && to) {
                    parts.arrows.push_back({*from, *to, *color});
                }
            });
        } else if (name == "csl") {
            forEachEntry(args, [&](std::string_view e) {
                const auto color = e.size() == 3 ? colorFromLetter(e[0]) : std::nullopt;
                const auto square = color ? Square::parse(e.substr(1, 2)) : std::nullopt;
                if (square) {
                    parts.squareMarks.push_back({*square, *color});
                }
            });
        } else {
            text.append(comment.substr(start, end - start + 1)); // keep unknown commands
        }
        pos = end + 1;
        // Removing a command shouldn't leave a double space behind.
        if ((name == "cal" || name == "csl") && pos < comment.size() && comment[pos] == ' ' &&
            (text.empty() || text.back() == ' ')) {
            ++pos;
        }
    }
    parts.text = std::string(trim(text));
    return parts;
}

std::string formatComment(const CommentParts& parts) {
    std::string result;
    if (!parts.squareMarks.empty()) {
        result += "[%csl ";
        for (std::size_t i = 0; i < parts.squareMarks.size(); ++i) {
            const auto& mark = parts.squareMarks[i];
            result += (i > 0 ? "," : "");
            result += letterFromColor(mark.color);
            result += mark.square.name();
        }
        result += ']';
    }
    if (!parts.arrows.empty()) {
        result += result.empty() ? "[%cal " : " [%cal ";
        for (std::size_t i = 0; i < parts.arrows.size(); ++i) {
            const auto& arrow = parts.arrows[i];
            result += (i > 0 ? "," : "");
            result += letterFromColor(arrow.color);
            result += arrow.from.name() + arrow.to.name();
        }
        result += ']';
    }
    if (!parts.text.empty()) {
        result += result.empty() ? parts.text : " " + parts.text;
    }
    return result;
}

namespace {

template <typename T, typename SamePlace>
void toggle(std::vector<T>& items, const T& item, SamePlace samePlace) {
    for (auto it = items.begin(); it != items.end(); ++it) {
        if (samePlace(*it, item)) {
            if (it->color == item.color) {
                items.erase(it);
            } else {
                it->color = item.color;
            }
            return;
        }
    }
    items.push_back(item);
}

} // namespace

void toggleArrow(std::vector<Arrow>& arrows, Arrow arrow) {
    toggle(arrows, arrow,
           [](const Arrow& a, const Arrow& b) { return a.from == b.from && a.to == b.to; });
}

void toggleSquareMark(std::vector<SquareMark>& marks, SquareMark mark) {
    toggle(marks, mark,
           [](const SquareMark& a, const SquareMark& b) { return a.square == b.square; });
}

// ---- NAGs --------------------------------------------------------------------------------------
namespace {

struct NagEntry {
    std::uint8_t nag;
    std::string_view symbol;
};

// The display symbols most chess software agrees on. Pairs ($22/$23, ...) are the same idea
// for White and Black.
constexpr std::array kNags{
    NagEntry{1, "!"},   NagEntry{2, "?"},   NagEntry{3, "!!"},  NagEntry{4, "??"},
    NagEntry{5, "!?"},  NagEntry{6, "?!"},  NagEntry{7, "□"},   NagEntry{10, "="},
    NagEntry{13, "∞"},  NagEntry{14, "⩲"},  NagEntry{15, "⩱"},  NagEntry{16, "±"},
    NagEntry{17, "∓"},  NagEntry{18, "+−"}, NagEntry{19, "−+"}, NagEntry{22, "⨀"},
    NagEntry{23, "⨀"},  NagEntry{32, "⟳"},  NagEntry{33, "⟳"},  NagEntry{36, "↑"},
    NagEntry{37, "↑"},  NagEntry{40, "→"},  NagEntry{41, "→"},  NagEntry{44, "=∞"},
    NagEntry{45, "=∞"}, NagEntry{132, "⇆"}, NagEntry{133, "⇆"}, NagEntry{138, "⊕"},
    NagEntry{139, "⊕"}, NagEntry{140, "∆"}, NagEntry{146, "N"},
};

// ASCII spellings found in older PGN files.
constexpr std::array kAsciiNags{
    NagEntry{14, "+/="}, NagEntry{15, "=/+"}, NagEntry{16, "+/-"},
    NagEntry{17, "-/+"}, NagEntry{18, "+-"},  NagEntry{19, "-+"},
};

} // namespace

std::string_view nagSymbol(std::uint8_t nag) noexcept {
    for (const auto& entry : kNags) {
        if (entry.nag == nag) {
            return entry.symbol;
        }
    }
    return {};
}

std::optional<std::uint8_t> nagFromSymbol(std::string_view symbol) noexcept {
    for (const auto& table :
         {std::span<const NagEntry>(kNags), std::span<const NagEntry>(kAsciiNags)}) {
        for (const auto& entry : table) {
            if (entry.symbol == symbol) {
                return entry.nag;
            }
        }
    }
    return std::nullopt;
}

} // namespace chessgui::core
