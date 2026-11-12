#pragma once

#include "kiebitz/core/game.hpp"

#include <QWidget>

#include <cstdint>
#include <memory>

class QComboBox;
class QHBoxLayout;
class QLabel;
class QStackedWidget;
class QVBoxLayout;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableView;

namespace kiebitz::gamedb {
class Database;
}

namespace kiebitz::app {

class ActivityChart;
class AppStyle;
class GamesModel;
class ResultBar;
class PageHeader;
class ProfileStore;
struct Profile;

/// The Profiles page: you and the people whose games you follow (friends, opponents). Each
/// profile has accounts (Lichess, chess.com) whose games are downloaded, and over-the-board
/// games you add as PGN; all of them together are the profile's database, which can be looked
/// at whole or one account at a time.
class ProfilesView : public QWidget {
    Q_OBJECT

public:
    ProfilesView(ProfileStore& store, const AppStyle& style, QWidget* parent = nullptr);

    /// The profile shown (its id).
    [[nodiscard]] QString current() const { return m_current; }
    void select(const QString& profileId);

signals:
    void openGameRequested(const kiebitz::core::Game& game);

private:
    void refreshList();
    void showProfile();
    void showAccounts(const Profile& profile);
    void showParts(const Profile& profile);
    void showGames();
    void showStats();
    void search();
    void newProfile();
    void addAccount();
    void addOtbGames();
    void showFideCard();

    ProfileStore& m_store;
    const AppStyle& m_style;
    QString m_current;
    PageHeader* m_header = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_name = nullptr;
    QPushButton* m_info = nullptr; ///< "i": the profile's FIDE player
    QLabel* m_summary = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_sync = nullptr;
    QPushButton* m_rename = nullptr;
    QPushButton* m_remove = nullptr;
    QVBoxLayout* m_accounts = nullptr; ///< the account cards, one above the other
    QComboBox* m_parts = nullptr;      ///< all games, one account, over the board
    QString m_part;                    ///< the part looked at ("" = all)
    std::uint8_t m_speeds = 0;         ///< the time control looked at (bits: 1 << Speed; 0: all)
    QStackedWidget* m_pages = nullptr; ///< Overview | Games
    QLabel* m_totalText = nullptr;     ///< "1,234 games · 54%"
    ResultBar* m_totalBar = nullptr;
    QLabel* m_legend = nullptr; ///< wins, draws, losses with their numbers
    QLabel* m_whiteText = nullptr;
    ResultBar* m_whiteBar = nullptr;
    QLabel* m_blackText = nullptr;
    ResultBar* m_blackBar = nullptr;
    ActivityChart* m_activity = nullptr;
    int m_statsRequest = 0;
    QLineEdit* m_search = nullptr;
    QTableView* m_games = nullptr;
    GamesModel* m_model = nullptr;
    std::shared_ptr<const gamedb::Database> m_db; ///< the games shown
    int m_searchRequest = 0;
};

} // namespace kiebitz::app
