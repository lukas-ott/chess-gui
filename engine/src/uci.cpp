#include "kiebitz/engine/uci.hpp"

#include <charconv>

namespace kiebitz::engine {
namespace {

/// Splits on spaces, skipping runs of them (engines are not always tidy).
class Tokens {
public:
    explicit Tokens(std::string_view text) : m_text(text) {}

    [[nodiscard]] std::optional<std::string_view> next() {
        while (m_pos < m_text.size() && isSpace(m_text[m_pos])) {
            ++m_pos;
        }
        if (m_pos >= m_text.size()) {
            return std::nullopt;
        }
        const std::size_t start = m_pos;
        while (m_pos < m_text.size() && !isSpace(m_text[m_pos])) {
            ++m_pos;
        }
        return m_text.substr(start, m_pos - start);
    }
    /// Everything that's left, trimmed.
    [[nodiscard]] std::string_view rest() {
        while (m_pos < m_text.size() && isSpace(m_text[m_pos])) {
            ++m_pos;
        }
        std::string_view remaining = m_text.substr(m_pos);
        while (!remaining.empty() && isSpace(remaining.back())) {
            remaining.remove_suffix(1);
        }
        m_pos = m_text.size();
        return remaining;
    }

private:
    static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
    std::string_view m_text;
    std::size_t m_pos = 0;
};

template <typename T> std::optional<T> number(std::optional<std::string_view> token) {
    if (!token) {
        return std::nullopt;
    }
    T value{};
    // from_chars reads the range [first, last): no terminating zero needed.
    const char* first = token->data(); // NOLINT(bugprone-suspicious-stringview-data-usage)
    const char* end = first + token->size();
    const auto [ptr, ec] = std::from_chars(first, end, value);
    if (ec != std::errc() || ptr != end) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<Info> parseInfo(std::string_view line) {
    Tokens tokens(line);
    if (tokens.next() != "info") {
        return std::nullopt;
    }
    Info info;
    bool hasScore = false;
    while (const auto token = tokens.next()) {
        if (token == "string") {
            return std::nullopt; // free text, never a search result
        }
        if (token == "multipv") {
            info.multiPv = number<int>(tokens.next()).value_or(1);
        } else if (token == "depth") {
            info.depth = number<int>(tokens.next()).value_or(0);
        } else if (token == "seldepth") {
            info.selDepth = number<int>(tokens.next()).value_or(0);
        } else if (token == "nodes") {
            info.nodes = number<std::uint64_t>(tokens.next()).value_or(0);
        } else if (token == "nps") {
            info.nps = number<std::uint64_t>(tokens.next()).value_or(0);
        } else if (token == "time") {
            info.timeMs = number<std::uint64_t>(tokens.next()).value_or(0);
        } else if (token == "score") {
            const auto kind = tokens.next();
            const auto value = number<int>(tokens.next());
            if (!value || (kind != "cp" && kind != "mate")) {
                return std::nullopt;
            }
            info.score.kind = kind == "cp" ? Score::Kind::Centipawns : Score::Kind::Mate;
            info.score.value = *value;
            hasScore = true;
        } else if (token == "lowerbound") {
            info.score.bound = Score::Bound::Lower;
        } else if (token == "upperbound") {
            info.score.bound = Score::Bound::Upper;
        } else if (token == "pv") {
            while (const auto move = tokens.next()) {
                info.pv.emplace_back(*move);
            }
        }
        // Anything else (hashfull, tbhits, wdl, currmove, ...) is skipped with its value read
        // as the next unknown token, which is harmless.
    }
    if (!hasScore || info.pv.empty()) {
        return std::nullopt;
    }
    return info;
}

std::optional<Option> parseOption(std::string_view line) {
    Tokens tokens(line);
    if (tokens.next() != "option" || tokens.next() != "name") {
        return std::nullopt;
    }
    // Names can contain spaces ("Skill Level"): everything up to "type".
    Option option;
    while (const auto token = tokens.next()) {
        if (token == "type") {
            break;
        }
        if (!option.name.empty()) {
            option.name += ' ';
        }
        option.name += *token;
    }
    option.type = std::string(tokens.next().value_or(""));
    if (option.name.empty() || option.type.empty()) {
        return std::nullopt;
    }
    while (const auto token = tokens.next()) {
        if (token == "default") {
            // A string default can be empty or contain spaces; it ends at the next keyword.
            std::string value;
            while (const auto part = tokens.next()) {
                if (part == "min" || part == "max" || part == "var") {
                    // Put the keyword back by handling it right here.
                    if (part == "var") {
                        option.choices.emplace_back(tokens.next().value_or(""));
                    } else if (part == "min") {
                        option.min = number<int>(tokens.next());
                    } else {
                        option.max = number<int>(tokens.next());
                    }
                    break;
                }
                if (!value.empty()) {
                    value += ' ';
                }
                value += *part;
            }
            option.defaultValue = value == "<empty>" ? std::string() : value;
        } else if (token == "min") {
            option.min = number<int>(tokens.next());
        } else if (token == "max") {
            option.max = number<int>(tokens.next());
        } else if (token == "var") {
            option.choices.emplace_back(tokens.next().value_or(""));
        }
    }
    return option;
}

std::optional<std::string> parseIdName(std::string_view line) {
    Tokens tokens(line);
    if (tokens.next() != "id" || tokens.next() != "name") {
        return std::nullopt;
    }
    const std::string_view name = tokens.rest();
    if (name.empty()) {
        return std::nullopt;
    }
    return std::string(name);
}

bool isBestMove(std::string_view line) {
    Tokens tokens(line);
    return tokens.next() == "bestmove";
}

std::string setOptionCommand(std::string_view name, std::string_view value) {
    std::string command = "setoption name ";
    command += name;
    command += " value ";
    command += value;
    return command;
}

std::string positionCommand(const core::Position& position) {
    return "position fen " + position.fen();
}

std::vector<core::Move> pvMoves(const core::Position& position,
                                const std::vector<std::string>& pv) {
    std::vector<core::Move> moves;
    moves.reserve(pv.size());
    core::Position current = position;
    for (const auto& uci : pv) {
        const auto move = current.parseUci(uci);
        if (!move.ok()) {
            break;
        }
        moves.push_back(*move);
        current.play(*move);
    }
    return moves;
}

} // namespace kiebitz::engine
