#pragma once

#include "kiebitz/core/game.hpp"

#include <QWidget>

#include <memory>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableView;

namespace kiebitz::gamedb {
class Database;
}

namespace kiebitz::app {

class ElidedLabel;

class AppStyle;
class DatabaseManager;
class DateRange;
class GamesModel;
class ElidedText;
class EmptyState;
class PageHeader;

/// The Databases page: your game databases on the left (with imports in progress), the
/// selected one's games on the right, all of them or what a search finds. Double-click a game
/// to open it on the board.
class DatabasesView : public QWidget {
    Q_OBJECT

public:
    DatabasesView(DatabaseManager& manager, const AppStyle& style, QWidget* parent = nullptr);

    /// Shows the Add Database dialog and starts the import it asks for.
    void addDatabase();
    /// Starts importing a file (also what the dialog does), after checking the disk space.
    void importFile(const QString& path, const QString& name, const QString& source = {});

signals:
    void openGameRequested(const kiebitz::core::Game& game);

private:
    void refreshList();
    void showSelected();
    void openRow(int row);

    DatabaseManager& m_manager;
    const AppStyle& m_style;
    PageHeader* m_header = nullptr;
    QStackedWidget* m_pages = nullptr; ///< 0: no databases yet, 1: the browser
    QListWidget* m_list = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QLineEdit* m_search = nullptr;
    DateRange* m_dates = nullptr;
    ElidedLabel* m_gamesStatus = nullptr; ///< "Searching…", "The first 10,000 are shown"
    QTableView* m_games = nullptr;
    GamesModel* m_model = nullptr;
    QString m_shown; ///< the folder of the database shown
    std::shared_ptr<const gamedb::Database> m_db;
};

} // namespace kiebitz::app
