#pragma once

#include "document/GameDocument.hpp"
#include "kiebitz/core/game.hpp"
#include "kiebitz/library/file_kind.hpp"
#include "repertoire/GapSource.hpp"

#include <QWidget>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableView;
class QTreeWidget;
class QTreeWidgetItem;

namespace kiebitz::app {

class AppStyle;
class BoardView;
class ElidedLabel;
class EmptyState;
class DateRange;
class GameFilter;
class GameListModel;
class PageHeader;
class RepertoireStore;
struct BoardTheme;
class Library;

/// The Library tab, as En Croissant's and ChessBase's file windows have it, in two cards:
///   Files: the library folder as a tree, with a search over the names and a filter by type;
///   the selection: what the selected item is.
///     - a folder: its name and what's in it;
///     - a game, tournament or other file: its games (a table that fits the type: a
///       tournament has rounds, puzzles show who is to move) and a preview board below;
///     - a repertoire file: an overview (positions to practise, lines), a preview of its line
///       from where it starts, and buttons to open and to practise it.
/// No paths are shown: the folder button shows the folder's name and picks another one.
///
/// Keyboard: the arrow keys act on whatever was clicked last (that card is outlined):
///   Files: ↑ ↓ move, ← → fold/unfold;
///   Games: ↑ ↓ pick a game, ← → step through its moves in the preview, Return opens it;
///   Preview (after a click on its board or step buttons): ← → step through the moves,
///            ↑ / Home start, ↓ / End end, Return opens the game.
class LibraryView : public QWidget {
    Q_OBJECT

public:
    LibraryView(Library& library, RepertoireStore& store, const AppStyle& style,
                QWidget* parent = nullptr);
    ~LibraryView() override;
    LibraryView(const LibraryView&) = delete;
    LibraryView& operator=(const LibraryView&) = delete;
    LibraryView(LibraryView&&) = delete;
    LibraryView& operator=(LibraryView&&) = delete;

    /// Selects a file by path (e.g. after opening a game from it). Does nothing if it isn't there.
    void selectFile(const QString& path);
    /// Only the repertoire files (of one side, or of both), in their folders: training's
    /// openings part, to pick one to practise.
    void showRepertoires(std::optional<core::Color> side);

    /// The preview board uses the same colours and pieces as the main board.
    void setBoardLook(const BoardTheme& theme, const QString& pieceSet);

    /// The keyboard handling for the game list and the preview board.
    bool eventFilter(QObject* watched, QEvent* event) override;

    /// Makes a folder `name` inside `parent` (relative to the library folder; empty = the top),
    /// and selects it once the library has seen it. Empty on success, else the error.
    QString createFolder(const QString& parent, const QString& name);
    /// Moves files and folders (absolute paths) into a folder of the library (relative; empty =
    /// the top). Open tabs follow (pathMoved). Empty on success, else the error.
    QString moveToFolder(const QStringList& paths, const QString& folder);

    /// Renames a file (without its ending: "Najdorf" for Najdorf.pgn) or a folder. Open tabs
    /// follow. Empty on success, else the error.
    QString rename(const QString& path, const QString& name);
    /// Moves a file or folder to the Trash; its open tabs close. Empty on success, else the
    /// error. Tests can put something else in place of the Trash.
    QString moveToTrash(const QString& path);
    void setTrashForTests(std::function<bool(const QString&)> trash) { m_trash = std::move(trash); }

    /// For tests: what the repertoire overview shows ("1,840", "412", "move 24").
    [[nodiscard]] QStringList repertoireStats() const;
    /// For tests: the gaps listed ("8… h6  14%  3% of games"), and the status above them.
    [[nodiscard]] QStringList repertoireGaps() const;
    [[nodiscard]] QString gapsStatus() const;

    /// Where the gap check asks how often moves are played (the Explorer's source, now).
    void setGapSource(std::function<GapSource()> source) { m_gapSource = std::move(source); }
    /// Checks the repertoire shown for gaps (the "Check" button).
    void checkGaps();

signals:
    void openGameRequested(std::shared_ptr<kiebitz::app::PgnFile> file, int index);
    /// Practise a repertoire file (its first game) from the start.
    void practiceRequested(std::shared_ptr<kiebitz::app::PgnFile> file);
    void changeFolderRequested();
    /// New File, into this folder (absolute).
    void newFileRequested(const QString& folder);
    /// A file or folder was moved (dragged in the tree): `from` and `to` are absolute paths.
    void pathMoved(const QString& from, const QString& to);
    /// A file or folder is about to go to the Trash: tabs of its games close.
    void pathRemoved(const QString& path);
    /// From a file's or folder's menu: set the type of these files (a library::FileKind; for a
    /// repertoire `side` is 0 = White, 1 = Black, else -1).
    void fileKindRequested(const QStringList& paths, int kind, int side);
    /// A gap, clicked: open the file at `line` (from the start) and play the opponent's `move`
    /// there, so you can add your answer.
    void gapRequested(const QString& path, const std::vector<kiebitz::core::Move>& line,
                      kiebitz::core::Move move);
    /// The gap check is done (tests).
    void gapsChecked();
    /// "Solve" on a puzzle file: its puzzles on the Puzzles page.
    void puzzlesRequested(const QString& path);
    /// The repertoire preview's "Open Here": the file at the position after `line`.
    void positionRequested(const QString& path, const std::vector<kiebitz::core::Move>& line);

private:
    void refreshFiles();
    void applyFilters();
    void filtersChanged();
    void showSelection();
    void showFolder(const QTreeWidgetItem& item);
    void showGames(const std::shared_ptr<PgnFile>& file, library::FileKind kind);
    void showRepertoire(const std::shared_ptr<PgnFile>& file);
    void refreshRepertoire();
    void showPreview();
    void stepPreview(int plies);
    void showPreviewPly(bool animate);
    void openSelectedGame();
    void updateIcons();
    void rememberExpanded();
    [[nodiscard]] int selectedRow() const;
    [[nodiscard]] std::shared_ptr<PgnFile> fileOf(const QTreeWidgetItem* item) const;
    /// The folder new things go into: the selected folder, or the selected file's folder
    /// (relative; empty = the top).
    [[nodiscard]] QString targetFolder() const;
    void askNewFolder();
    void askTrash(const QString& path);

    Library& m_library;
    RepertoireStore& m_store;
    const AppStyle& m_style;
    PageHeader* m_header = nullptr; ///< "Library", the counts, the folder button
    QPushButton* m_folderButton = nullptr;
    QStackedWidget* m_pages = nullptr; ///< the browser, or the "empty library" card

    // Files
    QLineEdit* m_fileSearch = nullptr;
    QComboBox* m_typeFilter = nullptr;          ///< all files, or only one type
    std::array<QPushButton*, 2> m_newButtons{}; ///< New File, New Folder
    QString m_selectAfterScan; ///< a new or renamed item (its key), selected when it shows up
    std::function<bool(const QString&)> m_trash; ///< tests only
    QTreeWidget* m_files = nullptr;

    // The selection
    QStackedWidget* m_detail = nullptr; ///< see Detail
    EmptyState* m_folderState = nullptr;
    std::shared_ptr<PgnFile> m_file; ///< the file shown
    library::FileKind m_kind = library::FileKind::Game;

    // Games
    QLabel* m_gamesIcon = nullptr;
    ElidedLabel* m_listTitle = nullptr; ///< the selected file; cut off if long
    QLabel* m_listCount = nullptr;
    QLineEdit* m_search = nullptr;
    QTableView* m_games = nullptr;
    GameListModel* m_model = nullptr;
    GameFilter* m_filter = nullptr; ///< search, dates and order
    DateRange* m_dates = nullptr;
    QPushButton* m_solvePuzzles = nullptr; ///< a puzzle file's "Solve"

    // Preview, under the games
    QStackedWidget* m_preview = nullptr; ///< a hint to pick a game, or the game
    BoardView* m_previewBoard = nullptr;
    QLabel* m_previewMove = nullptr; ///< "Final position" / "After 23. Nf3"
    QLabel* m_previewPlayers = nullptr;
    QLabel* m_previewDetails = nullptr;
    std::vector<QPushButton*> m_previewButtons; ///< start, back, forward, end
    std::optional<core::Game> m_previewGame;
    std::vector<core::NodeId> m_previewLine; ///< its main line, root first
    std::size_t m_previewPly = 0;            ///< index into m_previewLine
    int m_previewRequest = 0;                ///< newer selections win over slower, older parses

    // Repertoire overview
    QLabel* m_repIcon = nullptr;
    ElidedLabel* m_repTitle = nullptr;
    QLabel* m_repSubtitle = nullptr;
    std::array<QLabel*, 3> m_repStats{}; ///< positions, lines, deepest line
    int m_summaryRequest = 0;
    // The gap check
    std::function<GapSource()> m_gapSource;
    QPushButton* m_checkGaps = nullptr;
    // The repertoire's preview: its line from the start, stepped through as a game's
    BoardView* m_startBoard = nullptr;
    QLabel* m_repName = nullptr;   ///< the opening shown ("Caro-Kann Defense"), else the file
    QLabel* m_startLine = nullptr; ///< "Starts after 1. e4 c6"
    QLabel* m_repMove = nullptr;   ///< "After 2. d4"
    std::vector<QPushButton*> m_repButtons; ///< start, back, forward, end
    std::optional<core::Game> m_repGame;
    std::vector<core::NodeId> m_repLine; ///< root first
    std::size_t m_repPly = 0;            ///< index into m_repLine
    std::size_t m_startPly = 0;          ///< where the file starts, in m_repLine
    void showStart();
    void stepRepertoire(int plies);
    void showRepertoirePly(bool animate);
    void openRepertoireHere();
    QLabel* m_gapsStatus = nullptr;
    QTreeWidget* m_gaps = nullptr;
    std::vector<repertoire::Gap> m_gapList;
    std::shared_ptr<std::atomic<bool>> m_gapsCancel; ///< set when another file is shown
    int m_gapsRequest = 0;
    void resetGaps();

    std::array<QFrame*, 2> m_cards{};       ///< files, the selection: the focused one is outlined
    QMetaObject::Connection m_focusOutline; ///< to qApp's focusChanged; see ~LibraryView()
    bool m_rebuilding = false;              ///< while the tree is rebuilt, its signals are ignored
};

} // namespace kiebitz::app
