#include "kiebitz/core/annotations.hpp"
#include "kiebitz/pgn/pgn.hpp"

#include <algorithm>
#include <array>

namespace kiebitz::pgn {

using core::Game;
using core::NodeId;

namespace {

constexpr std::size_t kLineWidth = 80;

/// The Seven Tag Roster: every PGN game starts with these headers, in this order.
constexpr std::array<std::string_view, 7> kRoster{"Event", "Site",  "Date",  "Round",
                                                  "White", "Black", "Result"};
constexpr std::array<std::string_view, 7> kRosterDefaults{"?", "?", "????.??.??", "?",
                                                          "?", "?", "*"};

std::string escape(std::string_view value) {
    std::string result;
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            result += '\\';
        }
        result += c;
    }
    return result;
}

/// Collects movetext words and wraps them into lines of at most 80 characters.
class LineWriter {
public:
    void word(std::string_view word) {
        const std::string text = m_prefix + std::string(word);
        m_prefix.clear();
        if (!m_line.empty() && m_line.size() + 1 + text.size() > kLineWidth) {
            flush();
        }
        if (!m_line.empty()) {
            m_line += ' ';
        }
        m_line += text;
    }

    /// "(" sticks to the next word and ")" to the previous one: "(1... c5 2. Nf3)".
    void open() { m_prefix += '('; }
    void close() {
        if (m_line.empty()) {
            word(")");
        } else {
            m_line += ')';
        }
    }

    /// A comment, split into words so long comments wrap too. "}" can't appear inside.
    void comment(std::string_view text) {
        std::string clean(text);
        for (char& c : clean) {
            if (c == '}') {
                c = ')';
            } else if (c == '\n' || c == '\r') {
                c = ' ';
            }
        }
        std::vector<std::string> words;
        std::size_t start = 0;
        while (start < clean.size()) {
            const auto space = clean.find(' ', start);
            const auto end = space == std::string::npos ? clean.size() : space;
            if (end > start) {
                words.emplace_back(clean.substr(start, end - start));
            }
            start = end + 1;
        }
        // Never break inside a [%cal ...] command: keep each one as a single word.
        std::vector<std::string> merged;
        for (auto& w : words) {
            const bool insideCommand = !merged.empty() && merged.back().starts_with("[%") &&
                                       merged.back().find(']') == std::string::npos;
            if (insideCommand) {
                merged.back() += ' ' + w;
            } else {
                merged.push_back(std::move(w));
            }
        }
        words = std::move(merged);
        if (words.empty()) {
            return;
        }
        words.front().insert(0, "{");
        words.back() += '}';
        for (const auto& w : words) {
            word(w);
        }
    }

    [[nodiscard]] std::string finish() {
        flush();
        return std::move(m_out);
    }

private:
    void flush() {
        if (!m_line.empty()) {
            m_out += m_line;
            m_out += '\n';
            m_line.clear();
        }
    }

    std::string m_out;
    std::string m_line;
    std::string m_prefix;
};

class MovetextWriter {
public:
    explicit MovetextWriter(const Game& game) : m_game(&game) {}

    std::string write() {
        const core::Node& root = m_game->node(Game::root());
        writeComment(root);
        line(Game::root(), true);
        const auto result = m_game->header("Result");
        m_out.word(result.empty() ? "*" : result);
        return m_out.finish();
    }

private:
    /// The moves after `parent`, each followed by its alternatives in brackets.
    void line(NodeId parent, bool needNumber) {
        for (NodeId node = m_game->mainChild(parent); node != core::kNoNode;
             node = m_game->mainChild(node)) {
            needNumber = move(node, needNumber);
            for (NodeId alt = m_game->nextSibling(node); alt != core::kNoNode;
                 alt = m_game->nextSibling(alt)) {
                m_out.open();
                line(alt, move(alt, true));
                m_out.close();
                needNumber = true; // after a variation, the next move repeats its number
            }
        }
    }

    /// Writes one move with its number (when needed) and annotations. Returns whether the next
    /// move must show its number (after a comment it must).
    bool move(NodeId id, bool needNumber) {
        const core::Node& node = m_game->node(id);
        if (!node.commentBefore.empty()) {
            m_out.comment(node.commentBefore);
            needNumber = true;
        }
        const bool white = m_game->mover(id) == core::Color::White;
        std::string text;
        if (white || needNumber) {
            text = std::to_string(m_game->moveNumber(id)) + (white ? "." : "...");
            m_out.word(text);
        }
        m_out.word(node.san);
        for (const auto nag : node.nags) {
            m_out.word("$" + std::to_string(nag));
        }
        return writeComment(node);
    }

    bool writeComment(const core::Node& node) {
        const std::string comment =
            core::formatComment({node.comment, node.arrows, node.squareMarks});
        if (comment.empty()) {
            return false;
        }
        m_out.comment(comment);
        return true;
    }

    const Game* m_game; // not owned
    LineWriter m_out;
};

} // namespace

std::string writeGame(const Game& game) {
    std::string out;
    const auto tag = [&](std::string_view name, std::string_view value) {
        out += '[';
        out += name;
        out += " \"";
        out += escape(value);
        out += "\"]\n";
    };
    // The Seven Tag Roster first (filled in if missing), then the other headers in their order.
    for (std::size_t i = 0; i < kRoster.size(); ++i) {
        const auto value = game.header(kRoster[i]);
        tag(kRoster[i], value.empty() ? kRosterDefaults[i] : value);
    }
    for (const auto& [name, value] : game.headers()) {
        if (std::find(kRoster.begin(), kRoster.end(), name) == kRoster.end()) {
            tag(name, value);
        }
    }
    out += '\n';
    out += MovetextWriter(game).write();
    return out;
}

} // namespace kiebitz::pgn
