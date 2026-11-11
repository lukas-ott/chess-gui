#include "kiebitz/gamedb/scan.hpp"

#include "codec.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>

namespace kiebitz::gamedb {
namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/// `[Name "Value"]`: the name and the value (with \" and \\ unescaped).
bool parseTag(std::string_view line, std::string_view& name, std::string& value) {
    std::size_t i = 1;
    while (i < line.size() && isSpace(line[i])) {
        ++i;
    }
    const std::size_t nameStart = i;
    while (i < line.size() && !isSpace(line[i]) && line[i] != '"') {
        ++i;
    }
    name = line.substr(nameStart, i - nameStart);
    const std::size_t quote = line.find('"', i);
    if (quote == std::string_view::npos) {
        return false;
    }
    value.clear();
    for (std::size_t k = quote + 1; k < line.size(); ++k) {
        const char c = line[k];
        if (c == '\\' && k + 1 < line.size()) {
            value += line[++k];
        } else if (c == '"') {
            return true;
        } else {
            value += c;
        }
    }
    return true;
}

GameResult resultOf(std::string_view text) {
    if (text == "1-0") {
        return GameResult::WhiteWins;
    }
    if (text == "0-1") {
        return GameResult::BlackWins;
    }
    if (text == "1/2-1/2") {
        return GameResult::Draw;
    }
    return GameResult::Unknown;
}

int toInt(std::string_view text) {
    int value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

/// PGN writes unknown values as "?" (or "????.??.??"): stored as empty.
void keep(std::string& field, std::string& value) {
    if (value.find_first_not_of("?.") == std::string::npos) {
        value.clear();
    }
    field.swap(value);
}

} // namespace

ScannedGame scanGame(std::string_view text) {
    ScannedGame game;
    std::string value;
    std::string variant;
    std::size_t pos = text.starts_with("\xEF\xBB\xBF") ? 3 : 0; // a byte-order mark
    // Headers: lines starting with '['.
    while (pos < text.size()) {
        while (pos < text.size() && isSpace(text[pos])) {
            ++pos;
        }
        if (pos >= text.size() || text[pos] != '[') {
            break;
        }
        const std::size_t end = text.find('\n', pos);
        const std::string_view line =
            text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
        pos = end == std::string_view::npos ? text.size() : end + 1;
        std::string_view name;
        if (!parseTag(line, name, value)) {
            continue;
        }
        if (name == "White") {
            keep(game.white, value);
        } else if (name == "Black") {
            keep(game.black, value);
        } else if (name == "Event") {
            keep(game.event, value);
        } else if (name == "Site") {
            keep(game.site, value);
        } else if (name == "Date") {
            keep(game.date, value);
        } else if (name == "Round") {
            keep(game.round, value);
        } else if (name == "ECO") {
            keep(game.eco, value);
        } else if (name == "WhiteElo") {
            game.whiteElo = toInt(value);
        } else if (name == "BlackElo") {
            game.blackElo = toInt(value);
        } else if (name == "FEN") {
            game.fen = value;
        } else if (name == "Variant") {
            variant = value;
        } else if (name == "Result") {
            game.result = resultOf(value);
        }
    }
    if (!variant.empty() && variant != "Standard" && variant != "standard" &&
        variant != "From Position") {
        game.skipped = true; // Chess960 and others: their moves don't fit the codec
        return game;
    }
    chess::Board board;
    if (!game.fen.empty() && !board.setFen(game.fen)) {
        game.skipped = true;
        return game;
    }
    if (game.fen == chess::constants::STARTPOS) {
        game.fen.clear();
    }

    // The main line: SAN tokens, skipping move numbers, comments, variations and NAGs.
    chess::Movelist moves;
    std::array<char, 16> buffer{};
    while (pos < text.size()) {
        const char c = text[pos];
        if (isSpace(c) || c == ')') {
            ++pos;
        } else if (c == '{') {
            const std::size_t end = text.find('}', pos);
            pos = end == std::string_view::npos ? text.size() : end + 1;
        } else if (c == ';' || c == '%') {
            const std::size_t end = text.find('\n', pos);
            pos = end == std::string_view::npos ? text.size() : end + 1;
        } else if (c == '(') {
            // A variation, perhaps with comments and variations inside it.
            int depth = 0;
            while (pos < text.size()) {
                const char d = text[pos];
                if (d == '{') {
                    const std::size_t end = text.find('}', pos);
                    pos = end == std::string_view::npos ? text.size() : end + 1;
                    continue;
                }
                ++pos;
                if (d == '(') {
                    ++depth;
                } else if (d == ')' && --depth == 0) {
                    break;
                }
            }
        } else if (c == '$') {
            ++pos;
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
                ++pos;
            }
        } else if (c == '*') {
            break;
        } else {
            std::size_t end = pos;
            while (end < text.size() && !isSpace(text[end]) && text[end] != '{' &&
                   text[end] != '(' && text[end] != ')' && text[end] != ';') {
                ++end;
            }
            std::string_view token = text.substr(pos, end - pos);
            pos = end;
            if (token == "1-0" || token == "0-1" || token == "1/2-1/2") {
                break; // the result: the game is over
            }
            if (c >= '0' && c <= '9' && !token.starts_with("0-0")) {
                // A move number ("12.", "12...") perhaps with the move stuck to it ("12.Nf3").
                std::size_t skip = 0;
                while (skip < token.size() &&
                       ((token[skip] >= '0' && token[skip] <= '9') || token[skip] == '.')) {
                    ++skip;
                }
                token.remove_prefix(skip);
                if (token.empty()) {
                    continue;
                }
            }
            // A move: without "!?" and "+#", and "0-0" as "O-O".
            while (!token.empty() && (token.back() == '!' || token.back() == '?' ||
                                      token.back() == '+' || token.back() == '#')) {
                token.remove_suffix(1);
            }
            if (token.empty() || token.size() >= buffer.size()) {
                game.damaged = !token.empty();
                if (game.damaged) {
                    break;
                }
                continue;
            }
            std::copy_n(token.data(), token.size(), buffer.data());
            for (std::size_t k = 0; k < token.size(); ++k) {
                if (buffer[k] == '0') {
                    buffer[k] = 'O';
                }
            }
            if (token.starts_with("0-0")) {
                token = std::string_view(buffer.data(), token.size());
            }
            if (token == "--" || token == "Z0") {
                break; // a null move: the rest can't be stored
            }
            chess::Move move = chess::Move::NO_MOVE;
            try {
                move = chess::uci::parseSan(board, token, moves);
            } catch (const chess::uci::SanParseError&) {
                move = chess::Move::NO_MOVE;
            }
            const auto byte =
                move == chess::Move::NO_MOVE ? std::nullopt : detail::encode(board, move);
            if (!byte) {
                game.damaged = true;
                break;
            }
            game.moves += static_cast<char>(*byte);
            board.makeMove(move);
        }
    }
    // No headers and no moves (a readme in an archive, stray text): not a game.
    if (game.moves.empty() && game.white.empty() && game.black.empty() && game.event.empty() &&
        game.result == GameResult::Unknown) {
        game.skipped = true;
    }
    return game;
}

// ---- GameSplitter ------------------------------------------------------------------------------

void GameSplitter::feed(std::string_view text, const Sink& sink) {
    m_buffer.append(text);
    std::size_t pos = m_scanned;
    while (true) {
        const std::size_t end = std::string_view(m_buffer).find('\n', pos);
        if (end == std::string_view::npos) {
            break;
        }
        std::size_t first = pos;
        if (first == 0 && std::string_view(m_buffer).starts_with("\xEF\xBB\xBF")) {
            first = 3; // a byte-order mark at the very start
        }
        while (first < end &&
               (m_buffer[first] == ' ' || m_buffer[first] == '\t' || m_buffer[first] == '\r')) {
            ++first;
        }
        if (first < end) {
            if (m_buffer[first] == '[') {
                if (m_inMoves) { // a header after movetext: a new game begins here
                    sink(std::string_view(m_buffer).substr(m_gameStart, pos - m_gameStart));
                    m_gameStart = pos;
                    m_inMoves = false;
                }
            } else {
                m_inMoves = true;
            }
        }
        pos = end + 1;
    }
    m_scanned = pos;
    // Drop what has been handed out, so the buffer stays about one game long.
    if (m_gameStart > (1U << 20U)) {
        m_buffer.erase(0, m_gameStart);
        m_scanned -= m_gameStart;
        m_gameStart = 0;
    }
}

void GameSplitter::finish(const Sink& sink) {
    const std::string_view rest = std::string_view(m_buffer).substr(m_gameStart);
    if (rest.find_first_not_of(" \t\r\n") != std::string_view::npos) {
        sink(rest);
    }
    m_buffer.clear();
    m_scanned = 0;
    m_gameStart = 0;
    m_inMoves = false;
}

} // namespace kiebitz::gamedb
