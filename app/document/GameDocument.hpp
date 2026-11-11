#pragma once

#include "kiebitz/core/game.hpp"
#include "kiebitz/pgn/pgn.hpp"

#include <QString>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiebitz::app {

/// The text of a PGN file and where its games are. Shared by every tab showing a game from
/// that file (they all save into the same text).
struct PgnFile {
    QString path;                      ///< empty for text that came from the clipboard
    std::string text;                  ///< the whole file, if hasText
    std::vector<pgn::GameEntry> games; ///< from pgn::indexGames(text)
    /// False for the Library's files: it keeps only their index (from its cache), and a game is
    /// read from disk when it's needed. A game is only ever opened in a tab from a file with
    /// its text, because saving rewrites the whole file from it.
    bool hasText = true;
};

/// One open game, shown in one document tab.
struct GameDocument {
    int id = nextId(); ///< stable identity for the tab that shows it
    core::Game game;
    core::NodeId current = core::Game::root();
    std::shared_ptr<PgnFile> file; ///< where it came from; null for a new game
    int gameIndex = -1;            ///< which game of `file` it is; -1 for a new game
    bool modified = false;         ///< changed since it was opened or saved

    /// The tab title: "White – Black" when the players are known, else the file name, else
    /// "New game". A leading "• " marks unsaved changes.
    [[nodiscard]] QString title() const;
    /// Only a game that was never touched can be replaced by an opened file.
    [[nodiscard]] static int nextId() {
        static int counter = 0;
        return ++counter;
    }
    [[nodiscard]] bool isPristine() const { return !file && !modified && game.nodeCount() == 1; }
};

} // namespace kiebitz::app
