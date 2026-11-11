// Changing PGN text in place, so files keep their own formatting (and small diffs in git).

#include "kiebitz/pgn/pgn.hpp"
#include "tokenizer.hpp"

#include <algorithm>
#include <optional>

namespace kiebitz::pgn {
namespace {

using detail::Token;
using detail::TokenType;

std::string tagLine(std::string_view name, std::string_view value) {
    std::string line = "[";
    line += name;
    line += " \"";
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            line += '\\';
        }
        line += c;
    }
    line += "\"]";
    return line;
}

struct Edit {
    std::size_t at = 0;
    std::size_t remove = 0;
    std::string insert;
};

} // namespace

std::string setHeaderInAllGames(std::string_view text, std::string_view name,
                                std::optional<std::string_view> value) {
    std::vector<Edit> edits;
    // The game being read: where it starts, its last header, and the header to change.
    struct Game {
        std::size_t start = 0;
        std::optional<std::size_t> lastTagEnd;
        std::optional<Token> match;
    };
    std::optional<Game> current;
    bool inMoves = false;
    int depth = 0;
    const auto close = [&] {
        if (!current) {
            return;
        }
        if (current->match) {
            const Token& tag = *current->match;
            if (value) {
                edits.push_back({tag.offset, tag.end - tag.offset, tagLine(name, *value)});
            } else { // remove the header and the line break after it
                std::size_t end = tag.end;
                if (end < text.size() && text[end] == '\r') {
                    ++end;
                }
                if (end < text.size() && text[end] == '\n') {
                    ++end;
                }
                edits.push_back({tag.offset, end - tag.offset, {}});
            }
        } else if (value && current->lastTagEnd) {
            edits.push_back({*current->lastTagEnd, 0, "\n" + tagLine(name, *value)});
        } else if (value) {
            edits.push_back({current->start, 0, tagLine(name, *value) + "\n\n"});
        }
        current.reset();
        inMoves = false;
        depth = 0;
    };

    detail::Tokenizer tokens(text);
    for (Token token = tokens.next(); token.type != TokenType::End; token = tokens.next()) {
        if (token.type == TokenType::Tag && inMoves) {
            close(); // a header after moves starts the next game
        }
        if (!current) {
            current = Game{token.offset, std::nullopt, std::nullopt};
        }
        switch (token.type) {
        case TokenType::Tag:
            current->lastTagEnd = token.end;
            if (token.text == name && !current->match) {
                current->match = token;
            }
            break;
        case TokenType::VariationStart:
            ++depth;
            inMoves = true;
            break;
        case TokenType::VariationEnd:
            depth = std::max(0, depth - 1);
            break;
        case TokenType::Result:
            if (depth == 0) {
                close();
            }
            break;
        default:
            inMoves = true;
            break;
        }
    }
    close();

    std::string result(text);
    std::reverse(edits.begin(), edits.end()); // from the back: earlier offsets stay valid
    for (const Edit& edit : edits) {
        result.replace(edit.at, edit.remove, edit.insert);
    }
    return result;
}

} // namespace kiebitz::pgn
