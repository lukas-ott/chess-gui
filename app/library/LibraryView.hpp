#pragma once

#include "chessgui/core/game.hpp"
#include "document/GameDocument.hpp"

#include <QWidget>

#include <array>
#include <memory>
#include <optional>
#include <vector>

class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QStackedWidget;
class QTableView;
class QTreeWidget;
class QTreeWidgetItem;

namespace chessgui::app {

class AppStyle;
class BoardView;
class ElidedLabel;
class GameListModel;
struct BoardTheme;
class Library;

/// The Library tab, in three columns (a layout borrowed from ChessBase's database window, with
/// the look of Lichess / chess.com / En Croissant):
///   Files: the library folder as a tree; folders fold away, and selecting a folder lists every
///          game inside it;
///   Games: the selected file's or folder's games, with a search box;
///   Preview: the selected game on a small board you can step through.
/// No paths are shown: the folder button shows the folder's name and picks another one.
///
/// Keyboard: the arrow keys act on whatever was clicked last (that column is outlined) and
/// never move between the columns themselves:
///   Files: ↑ ↓ move, ← → fold/unfold;
///   Games: ↑ ↓ pick a game, ← → step through its moves in the preview, Return opens it;
///   Preview (after a click on its board or step buttons): ← → step through the moves,
///            ↑ / Home start, ↓ / End end, Return opens the game.
class LibraryView : public QWidget {
    Q_OBJECT

public:
    LibraryView(Library& library, const AppStyle& style, QWidget* parent = nullptr);
    ~LibraryView() override;
    LibraryView(const LibraryView&) = delete;
    LibraryView& operator=(const LibraryView&) = delete;
    LibraryView(LibraryView&&) = delete;
    LibraryView& operator=(LibraryView&&) = delete;

    /// Selects a file by path (e.g. after opening a game from it). Does nothing if it isn't there.
    void selectFile(const QString& path);
    /// The preview board uses the same colours and pieces as the main board.
    void setBoardLook(const BoardTheme& theme, const QString& pieceSet);

    /// The keyboard handling for the game list and the preview board.
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void openGameRequested(std::shared_ptr<chessgui::app::PgnFile> file, int index);
    void changeFolderRequested();

private:
    void refreshFiles();
    void showSelection();
    void showPreview();
    void stepPreview(int plies);
    void showPreviewPly(bool animate);
    void openSelectedGame();
    void updateIcons();
    void rememberExpanded();
    [[nodiscard]] int selectedRow() const;

    Library& m_library;
    const AppStyle& m_style;
    QLabel* m_status = nullptr;
    QPushButton* m_folderButton = nullptr;
    QTreeWidget* m_files = nullptr;
    ElidedLabel* m_listTitle = nullptr; ///< the selected file or folder; cut off if long
    QLabel* m_listCount = nullptr;
    QLineEdit* m_search = nullptr;
    QTableView* m_games = nullptr;
    QStackedWidget* m_pages = nullptr; ///< the browser, or the "empty library" card

    // Preview
    QStackedWidget* m_preview = nullptr; ///< details, or a hint to pick a game
    BoardView* m_previewBoard = nullptr;
    QLabel* m_previewMove = nullptr; ///< "Final position" / "After 23. Nf3"
    QLabel* m_previewPlayers = nullptr;
    QLabel* m_previewDetails = nullptr;
    std::vector<QPushButton*> m_previewButtons; ///< start, back, forward, end
    std::optional<core::Game> m_previewGame;
    std::vector<core::NodeId> m_previewLine; ///< its main line, root first
    std::size_t m_previewPly = 0;            ///< index into m_previewLine
    int m_previewRequest = 0;                ///< newer selections win over slower, older parses

    std::array<QFrame*, 3> m_cards{};       ///< files, games, preview: the focused one is outlined
    QMetaObject::Connection m_focusOutline; ///< to qApp's focusChanged; see ~LibraryView()
    bool m_rebuilding = false;              ///< while the tree is rebuilt, its signals are ignored
    GameListModel* m_model = nullptr;
    QSortFilterProxyModel* m_filter = nullptr;
};

} // namespace chessgui::app
