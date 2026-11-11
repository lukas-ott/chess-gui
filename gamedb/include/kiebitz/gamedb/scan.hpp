#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

/// Reading PGN for a database: fast, and only what a database keeps. A game's headers and its
/// main line; comments, variations and NAGs are skipped (a reference database is for searching
/// and statistics; annotated games belong in the Library).
namespace kiebitz::gamedb {

enum class GameResult : std::uint8_t { Unknown, WhiteWins, Draw, BlackWins };

/// One game as a database stores it.
struct ScannedGame {
    std::string white, black, event, site, date, round, eco;
    std::string fen; ///< the start position, if not the usual one
    int whiteElo = 0;
    int blackElo = 0;
    GameResult result = GameResult::Unknown;
    std::string moves; ///< one byte per move (see codec)
    /// The game couldn't be read to the end (an illegal or broken move): the moves before it
    /// are kept.
    bool damaged = false;
    /// A variant (Chess960, ...) or a set-up position that can't be read: not stored.
    bool skipped = false;
};

/// Reads one game (its text, from its first header to its result).
[[nodiscard]] ScannedGame scanGame(std::string_view text);

/// Cuts a stream of PGN into games. Feed it pieces of any size; it hands out each complete
/// game's text (valid only during the call). A game starts at a header line that follows
/// movetext.
class GameSplitter {
public:
    using Sink = std::function<void(std::string_view game)>;

    void feed(std::string_view text, const Sink& sink);
    /// The end of the stream: the last game.
    void finish(const Sink& sink);

private:
    std::string m_buffer;
    std::size_t m_scanned = 0;   ///< m_buffer is read up to here (a line start)
    std::size_t m_gameStart = 0; ///< where the current game began
    bool m_inMoves = false;      ///< the current game's movetext has started
};

} // namespace kiebitz::gamedb
