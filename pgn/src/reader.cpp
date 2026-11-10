#include "chessgui/core/annotations.hpp"
#include "chessgui/pgn/pgn.hpp"
#include "tokenizer.hpp"

#include <optional>

namespace chessgui::pgn {

using core::Error;
using core::Game;
using core::NodeId;
using core::Position;
using detail::Token;
using detail::TokenType;

std::string_view GameEntry::header(std::string_view name) const noexcept {
    for (const auto& [key, value] : headers) {
        if (key == name) {
            return value;
        }
    }
    return {};
}

namespace {

/// Undoes PGN's escapes in a tag value: \" → " and \\ → \.
std::string unescape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            ++i;
        }
        result += value[i];
    }
    return result;
}

/// Splits "Nf3!?" into the move and the NAG its suffix stands for.
std::pair<std::string_view, std::optional<std::uint8_t>> splitSuffix(std::string_view move) {
    std::size_t end = move.size();
    while (end > 0 && (move[end - 1] == '!' || move[end - 1] == '?')) {
        --end;
    }
    if (end == move.size()) {
        return {move, std::nullopt};
    }
    return {move.substr(0, end), core::nagFromSymbol(move.substr(end))};
}

/// Adds a comment's text and drawings to what a node already has (a move can have several).
void addComment(std::string& text, std::vector<core::Arrow>& arrows,
                std::vector<core::SquareMark>& marks, std::string_view comment) {
    auto parts = core::parseComment(comment);
    if (!parts.text.empty()) {
        text += text.empty() ? parts.text : " " + parts.text;
    }
    arrows.insert(arrows.end(), parts.arrows.begin(), parts.arrows.end());
    marks.insert(marks.end(), parts.squareMarks.begin(), parts.squareMarks.end());
}

/// Reads the movetext into a game. One Level per open variation: where the next move goes,
/// the position there, and the last move played at this level (variations branch off it).
class MovetextReader {
public:
    MovetextReader(Game& game, std::string_view text) : m_game(&game), m_text(text) {}

    std::optional<Error> read(detail::Tokenizer& tokens, Token token) {
        m_levels.push_back({Game::root(), m_game->startPosition(), core::kNoNode});
        for (; token.type != TokenType::End; token = tokens.next()) {
            if (token.type == TokenType::Result) {
                if (m_levels.size() == 1) {
                    break; // the game's result: done
                }
                continue; // some annotators end variations with a result: ignore it
            }
            std::optional<Error> error;
            switch (token.type) {
            case TokenType::Move:
                error = move(token);
                break;
            case TokenType::Comment:
                comment(token.text);
                break;
            case TokenType::Nag:
                nag(token.text);
                break;
            case TokenType::VariationStart:
                error = startVariation(token);
                break;
            case TokenType::VariationEnd:
                error = endVariation(token);
                break;
            case TokenType::Tag: // the next game's headers: this game is over
                return finish(token);
            default: // move numbers carry no information we need
                break;
            }
            if (error) {
                return error;
            }
        }
        return finish(token);
    }

private:
    struct Level {
        NodeId current;
        Position position;
        NodeId lastMove;
    };

    [[nodiscard]] Error at(const Token& token, const std::string& message) const {
        return Error{"Line " + std::to_string(detail::lineAt(m_text, token.offset)) + ": " +
                     message};
    }

    std::optional<Error> move(const Token& token) {
        Level& level = m_levels.back();
        const auto [san, suffixNag] = splitSuffix(token.text);
        if (san == "--" || san == "Z0") {
            return at(token, "null moves (\"--\") are not supported yet.");
        }
        const auto parsed = level.position.parseSan(san);
        if (!parsed) {
            const char* const dots =
                level.position.sideToMove() == core::Color::White ? ". " : "... ";
            return at(token, "move " + std::to_string(level.position.fullmoveNumber()) + dots +
                                 parsed.error().message);
        }
        const NodeId node = m_game->addMove(level.current, level.position, *parsed);
        level.position.play(*parsed);
        level.current = node;
        level.lastMove = node;
        if (suffixNag) {
            appendNag(node, *suffixNag);
        }
        if (!m_pendingBefore.empty()) {
            m_game->setCommentBefore(node, std::move(m_pendingBefore));
            m_pendingBefore.clear();
        }
        return std::nullopt;
    }

    void comment(std::string_view text) {
        const Level& level = m_levels.back();
        if (level.lastMove == core::kNoNode && m_levels.size() > 1) {
            // At the start of a variation: it belongs before the variation's first move.
            const auto parts = core::parseComment(text);
            m_pendingBefore += m_pendingBefore.empty() ? parts.text : " " + parts.text;
            return;
        }
        // After a move, or before the first move of the game (stored on the root).
        const NodeId target = level.lastMove == core::kNoNode ? Game::root() : level.lastMove;
        const core::Node& node = m_game->node(target);
        std::string commentText = node.comment;
        auto arrows = node.arrows;
        auto marks = node.squareMarks;
        addComment(commentText, arrows, marks, text);
        m_game->setComment(target, std::move(commentText));
        m_game->setArrows(target, std::move(arrows));
        m_game->setSquareMarks(target, std::move(marks));
    }

    void nag(std::string_view digits) {
        const NodeId target = m_levels.back().lastMove;
        int value = 0;
        for (const char c : digits) {
            value = (value * 10) + (c - '0');
        }
        if (target != core::kNoNode && value > 0 && value < 256) {
            appendNag(target, static_cast<std::uint8_t>(value));
        }
    }

    void appendNag(NodeId node, std::uint8_t value) {
        auto nags = m_game->node(node).nags;
        nags.push_back(value);
        m_game->setNags(node, std::move(nags));
    }

    std::optional<Error> startVariation(const Token& token) {
        const Level& level = m_levels.back();
        if (level.lastMove == core::kNoNode) {
            return at(token, "a variation \"(\" must follow the move it is an alternative to.");
        }
        // The variation replaces the last move: it starts from the position before it.
        const NodeId branch = m_game->parent(level.lastMove);
        m_levels.push_back({branch, m_game->positionAt(branch), core::kNoNode});
        return std::nullopt;
    }

    std::optional<Error> endVariation(const Token& token) {
        if (m_levels.size() <= 1) {
            return at(token, "\")\" closes a variation that was never opened.");
        }
        m_levels.pop_back();
        m_pendingBefore.clear();
        return std::nullopt;
    }

    std::optional<Error> finish(const Token& token) {
        if (m_levels.size() > 1) {
            return at(token, "a variation was opened with \"(\" but never closed.");
        }
        return std::nullopt;
    }

    Game* m_game; // not owned: the game being filled
    std::string_view m_text;
    std::vector<Level> m_levels;
    std::string m_pendingBefore;
};

} // namespace

core::Result<Game> parseGame(std::string_view text) {
    detail::Tokenizer tokens(text);
    Token token = tokens.next();

    Headers headers;
    for (; token.type == TokenType::Tag; token = tokens.next()) {
        headers.emplace_back(std::string(token.text), unescape(token.value));
    }

    std::optional<Position> start;
    for (const auto& [name, value] : headers) {
        if (name == "FEN") {
            auto position = Position::fromFen(value);
            if (!position) {
                return Error{"The game's FEN header is invalid: " + position.error().message};
            }
            start = std::move(*position);
        }
    }
    Game game = start ? Game(std::move(*start)) : Game();
    if (!headers.empty()) {
        game.setHeaders(std::move(headers));
    }

    MovetextReader reader(game, text);
    if (auto error = reader.read(tokens, token)) {
        return *error;
    }
    return game;
}

std::vector<GameEntry> indexGames(std::string_view text) {
    std::vector<GameEntry> games;
    detail::Tokenizer tokens(text);
    std::optional<GameEntry> current;
    bool inMoves = false; // seen movetext since the last header
    int depth = 0;

    const auto close = [&](std::size_t end) {
        if (current) {
            current->length = end - current->offset;
            games.push_back(std::move(*current));
            current.reset();
        }
        inMoves = false;
        depth = 0;
    };

    for (Token token = tokens.next(); token.type != TokenType::End; token = tokens.next()) {
        if (token.type == TokenType::Tag && inMoves) {
            close(token.offset); // a header after moves starts the next game
        }
        if (!current) {
            current = GameEntry{token.offset, 0, {}};
        }
        switch (token.type) {
        case TokenType::Tag:
            current->headers.emplace_back(std::string(token.text), unescape(token.value));
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
                close(token.end);
            }
            break;
        default:
            inMoves = true;
            break;
        }
    }
    close(text.size());
    return games;
}

core::Result<std::vector<Game>> parseAll(std::string_view text) {
    std::vector<Game> games;
    const auto entries = indexGames(text);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        auto game = parseGame(text.substr(entries[i].offset, entries[i].length));
        if (!game) {
            return Error{"Game " + std::to_string(i + 1) + ": " + game.error().message};
        }
        games.push_back(std::move(*game));
    }
    return games;
}

} // namespace chessgui::pgn
