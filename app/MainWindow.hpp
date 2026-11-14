#pragma once

#include "document/GameDocument.hpp"
#include "kiebitz/training/session.hpp"

#include <QMainWindow>
#include <QTimer>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class QAction;
class QActionGroup;
class QButtonGroup;
class QMenu;
class QStackedWidget;

namespace kiebitz::app {

class AppStyle;
class BoardPage;
class BoardView;
class Documents;
class EnginePanel;
class EngineRegistry;
class EnginesView;
class DatabaseManager;
class DatabasesView;
class LichessLogin;
class LichessPuzzles;
class MovesCard;
class Practice;
class ProfileStore;
class ProfilesView;
class SettingsView;
class Shortcuts;
class Sounds;
class TrainingBar;
class TrainingSession;
class TrainingHistory;
class TrainingView;
class ExplorerPanel;
class GameInfoPanel;
class Library;
class LibraryView;
class NotationView;
class PracticeStats;
class PuzzleStats;
class PuzzlesView;
class RepertoirePanel;
class RepertoireStore;

/// The main window: a sidebar of sections (the board with its game tabs, the Library, Puzzles,
/// ...) and the page of the section chosen. It ties the pieces together: the open games
/// (Documents), the board page (BoardPage, MovesCard and the panels) and practice (Practice).
///
/// Building the window, its pages and menus, and applying settings: MainWindowSetup.cpp.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /// The sidebar's sections, in the order of their pages.
    enum class Section : std::uint8_t {
        Board,
        Library,
        Training,
        Puzzles,
        Databases,
        Profiles,
        Engines,
        Settings
    };

    explicit MainWindow(AppStyle& style, QWidget* parent = nullptr);
    ~MainWindow() override;
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;

    /// Opens a PGN file (in the background) and shows its first game in a tab.
    void openFile(const QString& path);
    /// Opens one game of a file in a tab (or switches to it if it's already open).
    void openGame(const std::shared_ptr<PgnFile>& file, int index);
    /// Saves the current game to a path (for "Save As" and tests).
    void saveCurrentAs(const QString& path);
    [[nodiscard]] Library& library() { return *m_library; }
    [[nodiscard]] RepertoireStore& repertoires() { return *m_repertoires; }
    [[nodiscard]] EngineRegistry& engines() { return *m_engineRegistry; }
    void showSection(Section section);
    [[nodiscard]] Section section() const noexcept { return m_section; }
    /// The game of the tab shown, or null (tests).
    [[nodiscard]] const GameDocument* document() const { return doc(); }

signals:
    /// A file was opened or saved (or failed): for tests and the status bar.
    void fileOpened(const QString& path);
    void fileSaved(const QString& path);
    void fileError(const QString& message);
    /// The language changed and you chose to restart: the window has closed; main() starts
    /// Kiebitz again.
    void restartRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // Building (MainWindowSetup.cpp)
    void buildMenus();
    void buildFileMenu();
    void buildEditMenu();
    void buildGameMenu();
    void buildViewMenu(QMenu* menu);
    void buildLayout();
    void buildPages();
    [[nodiscard]] QWidget* buildSidebar();
    [[nodiscard]] QWidget* buildBoardPage();
    [[nodiscard]] QWidget* buildPanelsCard();
    void restoreState();
    /// A setting changed (or at the start): applied wherever it shows.
    void applySetting(const QString& key);
    /// The language changed: asks to restart now (then restartRequested()).
    void askToRestart();
    /// The Settings page's Folders section: where the library, databases, ... are.
    void showFolders();
    void showAbout();
    void chooseCustomColors();
    void chooseLibraryFolder();

    // Pages and tabs
    /// The current tab's game while the board is the page shown; else null.
    [[nodiscard]] GameDocument* doc() const;
    /// Shows what the current section and tab say: the game or another page.
    void updatePage();
    void updateSidebarIcons();
    void updateWindowTitle();
    void setModified();
    /// A message in the status bar; `ms` 0: until the next one.
    void showStatus(const QString& text, int ms);

    // Clipboard
    void copyPgn();
    void copyFen();
    void paste();
    /// Sends a key to the focused text field, if any. False if no text field has focus.
    static bool forwardToTextField(int key, Qt::KeyboardModifiers modifiers);
    void setUpPosition();

    /// File › New File: a name, a type and maybe PGN; created in the Library and opened.
    /// `folder`: where (absolute); empty = the Library folder.
    void newFile(const QString& folder = {});
    /// Opens a game (from a database) in a new tab, not tied to a file.
    void openGameCopy(const core::Game& game);

    // Repertoires
    /// Adds a line to a side's repertoire; asks before adding a second answer. False if
    /// cancelled.
    bool addLineAsking(core::Color side, const std::vector<core::Move>& line);
    /// Jump to Line: opens a repertoire file at a position (its hash).
    void jumpToLine(const QString& path, quint64 position);
    void goToPosition(std::uint64_t position);
    /// A repertoire file just opened: to where it starts (RepertoireStore::startOf).
    void goToFileStart();
    /// The side a tab's repertoire is for: a repertoire file's, or a whole repertoire's.
    [[nodiscard]] static std::optional<core::Color> repertoireSideOf(const GameDocument& d);
    /// Where the shown repertoire file starts, flagged in the move list.
    void showFileStart();
    /// "Starting Position" on a move: the file starts after it (saved as [StartsAt]).
    void setFileStart(core::NodeId id);
    [[nodiscard]] core::NodeId fileStartNode(const GameDocument& d) const;
    [[nodiscard]] std::vector<core::Move> sharedLineOf(const GameDocument& d) const;
    void updateMovesTitle();
    /// The board from the side a game's [Orientation] header names (repertoires and studies
    /// have one); other games keep the side the user chose last.
    void orientBoard(const GameDocument& document);

    // Practice (Practice does the practising; these start it and show it)
    /// `mistakes`: only the lines through the positions you still get wrong.
    void startPractice(bool fromHere, int depth, bool mistakes = false);
    /// Every repertoire file of the shown one's side, merged in a tab of its own, practised.
    void practiceWholeRepertoire(int depth);
    /// Training: a part starts; its page.
    void onTrainingPart(training::Part part);
    void onPracticeStarted(core::Color side);
    void onPracticeStopped();

    // Showing the game
    void showNode(core::NodeId id, bool animate);
    void showDocument();
    void refreshNotation(); ///< after the tree changed
    /// The players' clocks above and below the board, from the [%clk] comments up to the
    /// current move; hidden for games without clocks.
    void refreshClocks();
    void onMoveMade(core::Move move);
    /// Plays moves from the current position (a line clicked in the engine panel), as a new
    /// variation where they aren't in the game yet.
    void playMoves(const std::vector<core::Move>& moves);
    void goBack();
    void goForward();
    void goToStart();
    void goToEnd();
    void stepChoiceOrJump(int step);
    void updateNavigation();
    void updateFork();
    void updateIcons();
    static void markContinuationsLearned();

    // Editing the tree
    void promote(core::NodeId id);
    void makeMainLine(core::NodeId id);
    void deleteFrom(core::NodeId id);
    void deleteVariation(core::NodeId id);
    void toggleNag(core::NodeId id, std::uint8_t nag);
    void clearNags(core::NodeId id);
    void editComment(core::NodeId id);
    void onCommentEdited(const QString& text);
    void onHeaderEdited(const QString& name, const QString& value);

    AppStyle& m_style;
    Library* m_library = nullptr;
    Documents* m_documents = nullptr; ///< the open games and their tabs
    Section m_section = Section::Board;

    // Pages
    QStackedWidget* m_pages = nullptr; ///< one per section, in Section order
    QButtonGroup* m_sidebar = nullptr; ///< one button per section, ids = Section
    LibraryView* m_libraryView = nullptr;
    DatabaseManager* m_databases = nullptr;
    DatabasesView* m_databasesView = nullptr;
    LichessPuzzles* m_lichessPuzzles = nullptr;
    PuzzleStats* m_puzzleStats = nullptr;
    PuzzlesView* m_puzzlesView = nullptr;
    LichessLogin* m_lichess = nullptr; ///< signed in: Lichess explorer, faster downloads
    ProfileStore* m_profiles = nullptr;
    ProfilesView* m_profilesView = nullptr;
    SettingsView* m_settingsView = nullptr;
    EngineRegistry* m_engineRegistry = nullptr;
    EnginesView* m_enginesView = nullptr;
    Sounds* m_sounds = nullptr; ///< the board's move sounds
    TrainingHistory* m_trainingHistory = nullptr;
    TrainingSession* m_training = nullptr;
    TrainingView* m_trainingView = nullptr;
    TrainingBar* m_trainingBar = nullptr; ///< the running session, in the status bar
    bool m_trainingFilter = false;        ///< the openings part filtered the Library: undo it after

    // The board page
    BoardPage* m_boardPage = nullptr;
    BoardView* m_board = nullptr; ///< m_boardPage's
    MovesCard* m_moves = nullptr;
    NotationView* m_notation = nullptr; ///< m_moves's
    EnginePanel* m_enginePanel = nullptr;
    ExplorerPanel* m_explorer = nullptr;
    RepertoireStore* m_repertoires = nullptr;
    RepertoirePanel* m_repertoirePanel = nullptr; ///< the Repertoire tab beside a game
    GameInfoPanel* m_info = nullptr;
    QTimer m_notationRefresh; ///< typing a comment re-renders the notation after a pause
    int m_choice = 0;         ///< chosen continuation at a branch point (index into the children)

    // Practice
    Practice* m_practice = nullptr;
    PracticeStats* m_practiceStats = nullptr;
    QString m_practiceOnOpen;              ///< a file to practise as soon as its tab has opened
    std::array<bool, 2> m_mistakesAsked{}; ///< White, Black: their mistakes are being read
    /// A repertoire file to show at a position (hash) as soon as its tab has opened.
    std::optional<std::pair<QString, std::uint64_t>> m_jumpOnOpen;
    std::optional<core::Move> m_playOnJump; ///< a gap's move: played once the jump is done

    // Menus
    QAction* m_backAction = nullptr;
    QAction* m_forwardAction = nullptr;
    QAction* m_startAction = nullptr;
    QAction* m_endAction = nullptr;
    QAction* m_upAction = nullptr;   ///< ↑: the previous continuation (not in a menu)
    QAction* m_downAction = nullptr; ///< ↓: the next one
    QAction* m_flipAction = nullptr;
    QAction* m_coordinatesAction = nullptr;
    std::vector<QAction*> m_gameActions; ///< only make sense on a game tab
    Shortcuts* m_shortcuts = nullptr;    ///< every command's key, and ← → ↑ ↓ for the game
    QActionGroup* m_themeGroup = nullptr;
    QAction* m_customColorsAction = nullptr;
    QActionGroup* m_piecesGroup = nullptr;
    QActionGroup* m_appThemeGroup = nullptr; ///< View › Theme
};

} // namespace kiebitz::app
