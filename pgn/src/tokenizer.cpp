#include "tokenizer.hpp"

#include <algorithm>

namespace kiebitz::pgn::detail {
namespace {

bool isSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool isDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

/// Characters that end a move or result token.
bool isDelimiter(char c) noexcept {
    return isSpace(c) || c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' ||
           c == ';' || c == '$';
}

} // namespace

Token Tokenizer::next() {
    while (m_pos < m_text.size()) {
        const char c = m_text[m_pos];
        if (isSpace(c)) {
            ++m_pos;
            continue;
        }
        const std::size_t start = m_pos;
        // "%" at the start of a line escapes the whole line (PGN standard, section 6).
        if (c == '%' && (start == 0 || m_text[start - 1] == '\n')) {
            const auto eol = m_text.find('\n', start);
            m_pos = eol == std::string_view::npos ? m_text.size() : eol;
            continue;
        }
        switch (c) {
        case '[':
            return readTag(start);
        case '{': {
            const auto close = m_text.find('}', start + 1);
            const std::size_t end = close == std::string_view::npos ? m_text.size() : close;
            m_pos = std::min(end + 1, m_text.size());
            return {
                TokenType::Comment, m_text.substr(start + 1, end - start - 1), {}, start, m_pos};
        }
        case ';': { // comment to the end of the line
            const auto eol = m_text.find('\n', start);
            const std::size_t end = eol == std::string_view::npos ? m_text.size() : eol;
            m_pos = end;
            return {TokenType::Comment, m_text.substr(start + 1, end - start - 1), {}, start, end};
        }
        case '(':
            ++m_pos;
            return {TokenType::VariationStart, m_text.substr(start, 1), {}, start, m_pos};
        case ')':
            ++m_pos;
            return {TokenType::VariationEnd, m_text.substr(start, 1), {}, start, m_pos};
        case '$': {
            ++m_pos;
            while (m_pos < m_text.size() && isDigit(m_text[m_pos])) {
                ++m_pos;
            }
            return {TokenType::Nag, m_text.substr(start + 1, m_pos - start - 1), {}, start, m_pos};
        }
        case '}':
        case ']':
            ++m_pos; // stray closing bracket: skip
            continue;
        default:
            break;
        }
        if (m_text.substr(start).starts_with("0.5-0.5")) { // a draw, not move number "0."
            return readWord(start);
        }
        if (isDigit(c)) {
            // "12." / "12..." is a move number (possibly glued to the move: "12.Nf3").
            std::size_t p = m_pos;
            while (p < m_text.size() && isDigit(m_text[p])) {
                ++p;
            }
            if (p < m_text.size() && m_text[p] == '.') {
                while (p < m_text.size() && m_text[p] == '.') {
                    ++p;
                }
                m_pos = p;
                return {TokenType::MoveNumber, m_text.substr(start, p - start), {}, start, p};
            }
        }
        return readWord(start);
    }
    return {TokenType::End, {}, {}, m_text.size(), m_text.size()};
}

Token Tokenizer::readTag(std::size_t start) {
    // [Name "Value"], with \" and \\ escapes inside the value.
    std::size_t p = start + 1;
    while (p < m_text.size() && isSpace(m_text[p])) {
        ++p;
    }
    const std::size_t nameStart = p;
    while (p < m_text.size() && !isSpace(m_text[p]) && m_text[p] != '"' && m_text[p] != ']') {
        ++p;
    }
    const std::string_view name = m_text.substr(nameStart, p - nameStart);
    std::string_view value;
    const auto quote = m_text.find('"', p);
    const auto lineEnd = m_text.find('\n', p);
    if (quote != std::string_view::npos && (lineEnd == std::string_view::npos || quote < lineEnd)) {
        std::size_t q = quote + 1;
        while (q < m_text.size() && m_text[q] != '"' && m_text[q] != '\n') {
            q += m_text[q] == '\\' ? 2 : 1;
        }
        q = std::min(q, m_text.size());
        value = m_text.substr(quote + 1, q - quote - 1);
        p = q;
    }
    // Up to and including the closing bracket (or the end of the line if it's missing).
    while (p < m_text.size() && m_text[p] != ']' && m_text[p] != '\n') {
        ++p;
    }
    if (p < m_text.size() && m_text[p] == ']') {
        ++p;
    }
    m_pos = p;
    return {TokenType::Tag, name, value, start, p};
}

Token Tokenizer::readWord(std::size_t start) {
    std::size_t p = start;
    while (p < m_text.size() && !isDelimiter(m_text[p])) {
        ++p;
    }
    if (p == start) { // a lone delimiter we don't handle: skip it
        m_pos = start + 1;
        return next();
    }
    m_pos = p;
    const std::string_view word = m_text.substr(start, p - start);
    // Also the non-standard draws some tools write: "0.5-0.5" and "½-½".
    const bool result = word == "1-0" || word == "0-1" || word == "1/2-1/2" || word == "*" ||
                        word == "0.5-0.5" || word == "\xC2\xBD-\xC2\xBD";
    return {result ? TokenType::Result : TokenType::Move, word, {}, start, p};
}

std::size_t lineAt(std::string_view text, std::size_t offset) {
    const auto prefix = text.substr(0, std::min(offset, text.size()));
    return static_cast<std::size_t>(std::count(prefix.begin(), prefix.end(), '\n')) + 1;
}

} // namespace kiebitz::pgn::detail
