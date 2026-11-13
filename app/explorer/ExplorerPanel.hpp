#pragma once

#include "kiebitz/core/game.hpp"
#include "kiebitz/gamedb/database.hpp"
#include "repertoire/GapSource.hpp"

#include <QWidget>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class QComboBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTreeWidget;

namespace kiebitz::gamedb {
class Database;
}

namespace kiebitz::app {

class AppStyle;
class DatabaseManager;
class LichessExplorer;
class LichessLogin;
class ProfileStore;
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
    ExplorerPanel(DatabaseManager& databases, ProfileStore& profiles, RepertoireStore& repertoires,
                  LichessLogin& lichess, const AppStyle& style, QWidget* parent = nullptr);

    /// What the gap check asks: the source chosen here, with its filter.
    [[nodiscard]] GapSource gapSource();

    /// For tests: Lichess's explorer.
    [[nodiscard]] LichessExplorer& lichessExplorer() { return *m_online; }

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
    QWidget* buildFilter();
    void filterChanged();
    /// The chosen source's own filter, into m_filter and the controls.
    void loadFilter();
    /// Where a source's filter is kept in the settings.
    [[nodiscard]] static QString filterGroup(const QString& source);
    /// The "Games from" choices for the profile chosen (none for a database).
    void refreshParts();
    /// The database of the chosen source (a database, a profile, or a part of one), or null.
    [[nodiscard]] std::shared_ptr<const gamedb::Database> sourceDatabase() const;
    void refresh();
    /// The Lichess or masters explorer is chosen (not a database or profile).
    [[nodiscard]] bool onlineSource() const;
    /// Puts an answer in the lists; `repertoire`: your moves here, marked.
    void showAnswer(const gamedb::Explored& explored, const std::vector<gamedb::GameInfo>& games,
                    const std::vector<core::Move>& repertoire);

    DatabaseManager& m_databases;
    ProfileStore& m_profiles;
    RepertoireStore& m_repertoires;
    LichessLogin& m_lichess;
    LichessExplorer* m_online = nullptr;
    std::vector<QString> m_onlineIds; ///< the top games' Lichess ids (online sources)
    QWidget* m_signIn = nullptr;      ///< "Sign in to use Lichess's explorer"
    std::vector<QWidget*> m_nameRows; ///< White / Black name filters: not for online sources
    const AppStyle& m_style;
    const core::Game* m_game = nullptr;
    core::NodeId m_current = core::Game::root();
    core::Color m_viewSide = core::Color::White;
    int m_request = 0;

    QStackedWidget* m_pages = nullptr; ///< 0: no databases, 1: explorer, 2: not from the start
    QComboBox* m_source = nullptr;
    bool m_sourceChosen = false; ///< picked (now or before); else the first one, which may change
    QLabel* m_summary = nullptr;
    ResultBar* m_total = nullptr;
    QTreeWidget* m_moves = nullptr;
    QTreeWidget* m_games = nullptr;
    gamedb::ExploreFilter m_filter;
    QPushButton* m_filterTab = nullptr; ///< "Filter", with a dot while one is set
    QWidget* m_sideRow = nullptr;       ///< "Played as": only for profiles
    QWidget* m_partRow = nullptr;       ///< "Games from": a profile's accounts, OTB
    QWidget* m_sideTitle = nullptr;     ///< the captions of those two rows
    QWidget* m_partTitle = nullptr;
    QComboBox* m_partBox = nullptr; ///< in m_partRow
    QString m_part;                 ///< a profile's part shown: "" (all), an account's id, or "otb"
    std::function<void()> m_showFilter; ///< puts m_filter into the filter's controls
    QString m_filterSource;             ///< the source m_filter belongs to
    bool m_loadingFilter = false;       ///< the controls are being set: no saving meanwhile
};

} // namespace kiebitz::app
