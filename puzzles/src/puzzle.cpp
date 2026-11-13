#include "kiebitz/puzzles/puzzle.hpp"

#include <array>
#include <cctype>
#include <charconv>

namespace kiebitz::puzzles {
namespace {

/// The fields of a CSV line without quotes (Lichess's puzzle file has none).
std::vector<std::string_view> splitFields(std::string_view line, char separator) {
    std::vector<std::string_view> fields;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = line.find(separator, begin);
        fields.push_back(line.substr(begin, end == std::string_view::npos ? end : end - begin));
        if (end == std::string_view::npos) {
            return fields;
        }
        begin = end + 1;
    }
}

} // namespace

std::optional<Puzzle> parseLichessLine(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }
    const std::vector<std::string_view> fields = splitFields(line, ',');
    if (fields.size() < 8) {
        return std::nullopt;
    }
    int rating = 0;
    const std::string_view ratingText = fields[3];
    if (std::from_chars(ratingText.data(), ratingText.data() + ratingText.size(), rating).ec !=
        std::errc()) {
        return std::nullopt; // the header line, or a broken one
    }
    auto start = core::Position::fromFen(fields[1]);
    if (!start) {
        return std::nullopt;
    }
    Puzzle puzzle;
    puzzle.id = std::string(fields[0]);
    puzzle.rating = rating;
    puzzle.opponentFirst = true;
    puzzle.game = core::Game(*start);
    core::Position position = *start;
    core::NodeId node = core::Game::root();
    for (const std::string_view uci : splitFields(fields[2], ' ')) {
        if (uci.empty()) {
            continue;
        }
        const auto move = position.parseUci(uci);
        if (!move) {
            return std::nullopt;
        }
        node = puzzle.game.addMove(node, position, *move);
        position.play(*move);
    }
    if (puzzle.game.ply(node) < 2) {
        return std::nullopt; // the opponent's move and at least one of yours
    }
    for (const std::string_view theme : splitFields(fields[7], ' ')) {
        if (!theme.empty()) {
            puzzle.themes.emplace_back(theme);
        }
    }
    return puzzle;
}

std::optional<Puzzle> fromGame(core::Game game) {
    if (game.mainChild(core::Game::root()) == core::kNoNode) {
        return std::nullopt;
    }
    Puzzle puzzle;
    puzzle.game = std::move(game);
    return puzzle;
}

std::string themeName(std::string_view theme) {
    // Lichess's names that don't read well split at the capitals.
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 8> kNames{{
        {"mateIn1", "Mate in 1"},
        {"mateIn2", "Mate in 2"},
        {"mateIn3", "Mate in 3"},
        {"mateIn4", "Mate in 4"},
        {"mateIn5", "Mate in 5"},
        {"oneMove", "One move"},
        {"enPassant", "En passant"},
        {"xRayAttack", "X-ray attack"},
    }};
    for (const auto& [key, name] : kNames) {
        if (key == theme) {
            return std::string(name);
        }
    }
    std::string name;
    for (std::size_t i = 0; i < theme.size(); ++i) {
        const char c = theme[i];
        if (std::isupper(static_cast<unsigned char>(c)) != 0 && i > 0) {
            name += ' ';
            name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            name += i == 0 ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        }
    }
    return name;
}

// ---- Solve -------------------------------------------------------------------------------------

Solve::Solve(const Puzzle& puzzle) : m_game(puzzle.game) {
    if (puzzle.opponentFirst) {
        m_start = m_game.mainChild(core::Game::root());
    }
    m_node = m_start;
    m_solver = m_game.positionAt(m_start).sideToMove();
}

Solve::Verdict Solve::play(core::Move move) {
    if (m_solved || awaitingReply()) {
        return Verdict::Wrong;
    }
    const core::NodeId next = m_game.mainChild(m_node);
    if (next == core::kNoNode) {
        return Verdict::Wrong;
    }
    if (m_game.node(next).move == move) {
        m_node = next;
        if (m_game.mainChild(m_node) == core::kNoNode) {
            m_solved = true;
            return Verdict::Solved;
        }
        return Verdict::Right;
    }
    // Another move that mates solves it too.
    const core::Position position = m_game.positionAt(m_node);
    if (position.isLegal(move)) {
        core::Position after = position;
        after.play(move);
        if (after.isCheckmate()) {
            m_node = m_game.addMove(m_node, position, move);
            m_solved = true;
            return Verdict::Solved;
        }
    }
    m_failed = true;
    return Verdict::Wrong;
}

bool Solve::awaitingReply() const {
    return !m_solved && m_node != m_start && m_game.mover(m_node) == m_solver &&
           m_game.mainChild(m_node) != core::kNoNode;
}

core::NodeId Solve::reply() {
    if (!awaitingReply()) {
        return core::kNoNode;
    }
    m_node = m_game.mainChild(m_node);
    if (m_game.mainChild(m_node) == core::kNoNode) {
        m_solved = true; // a file's solution may end with the opponent's move
    }
    return m_node;
}

std::optional<core::Move> Solve::expected() const {
    if (m_solved || awaitingReply()) {
        return std::nullopt;
    }
    const core::NodeId next = m_game.mainChild(m_node);
    return next == core::kNoNode ? std::nullopt : std::optional(m_game.node(next).move);
}

} // namespace kiebitz::puzzles
