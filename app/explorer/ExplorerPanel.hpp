#pragma once

#include "kiebitz/core/game.hpp"

#include <QWidget>

#include <cstdint>

class QComboBox;
class QLabel;
class QStackedWidget;
class QTreeWidget;

namespace kiebitz::app {

class AppStyle;
class DatabaseManager;
class RepertoireStore;
class ResultBar;

/// The Explorer tab: what is played in the position on the board, from a game database (Lumbra's
/// Gigabase, Caissabase, your own). Every next move with how often it was played, how it scored
/// for White and Black, and both sides' average rating; the moves of your repertoire (for the
/// side the board is viewed from) are marked. Click a move to play it. Top Games: the best rated
/// games of the position; double-click one to open it in a new tab.
class ExplorerPanel : public QWidget {
    Q_OBJECT

public:
    ExplorerPanel(DatabaseManager& databases, RepertoireStore& repertoires, const AppStyle& style,
                  QWidget* parent = nullptr);

    /// The game and move shown (null: none). The game must outlive the panel or the next call.
    void setPosition(const core::Game* game, core::NodeId current, core::Color viewSide);

    /// For tests: the moves listed ("e4  3  …"), once the explorer has answered.
    [[nodiscard]] QStringList moveTexts() const;

signals:
    void movePlayed(kiebitz::core::Move move);
    void openGameRequested(const kiebitz::core::Game& game);
    void showDatabasesRequested();
    /// The explorer has answered for the current position (tests wait for it).
    void updated();

private:
    void refreshSources();
    void refresh();

    DatabaseManager& m_databases;
    RepertoireStore& m_repertoires;
    const AppStyle& m_style;
    const core::Game* m_game = nullptr;
    core::NodeId m_current = core::Game::root();
    core::Color m_viewSide = core::Color::White;
    int m_request = 0;

    QStackedWidget* m_pages = nullptr; ///< 0: no databases, 1: explorer, 2: not from the start
    QComboBox* m_source = nullptr;
    QLabel* m_summary = nullptr;
    ResultBar* m_total = nullptr;
    QTreeWidget* m_moves = nullptr;
    QTreeWidget* m_games = nullptr;
};

} // namespace kiebitz::app
