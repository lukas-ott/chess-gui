#pragma once

#include "document/GameDocument.hpp"

#include <QMainWindow>
#include <QTimer>

#include <memory>
#include <vector>

class QAction;
class QActionGroup;
class QKeySequence;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QShortcut;
class QSplitter;
class QStackedWidget;
class QTabBar;
class QTabWidget;
class QVBoxLayout;

namespace chessgui::app {

class AppStyle;
class BoardView;
class GameInfoPanel;
class Library;
class LibraryView;
class NotationView;

/// The main window. Document tabs along the top: the Library (always first) and one tab per
/// open game. A game tab shows the board on the left and, on the right, the moves (notation,
/// comment box, navigation) above the panels (Engine, Explorer, Repertoire, Info).
///
/// Each game tab owns a GameDocument. Moves played on the board are added to its tree (as a
/// new variation when they differ from what's there); edits mark it modified until saved.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
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

signals:
    /// A file was opened or saved (or failed): for tests and the status bar.
    void fileOpened(const QString& path);
    void fileSaved(const QString& path);
    void fileError(const QString& message);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // Building
    void buildMenus();
    void buildFileMenu();
    void buildEditMenu();
    void buildGameMenu();
    void buildViewMenu(QMenu* menu);
    void bindPlainKey(QAction* action, const QKeySequence& key);
    void buildLayout();
    [[nodiscard]] QWidget* buildAnalysisPage();
    [[nodiscard]] QWidget* buildMovesCard();
    [[nodiscard]] QWidget* buildPanelsCard();
    void restoreState();
    void fitBoardColumn();
    void showAbout();
    void chooseCustomColors();
    void chooseLibraryFolder();

    // Documents and tabs
    [[nodiscard]] GameDocument* doc() const;
    [[nodiscard]] GameDocument* docAt(int tab) const;
    void addDocument(std::unique_ptr<GameDocument> document);
    void onTabChanged(int tab);
    void closeTab(int tab);
    /// Asks to save a modified game. False if the user cancelled.
    bool maybeSave(GameDocument& document);
    void save();
    void saveAs();
    void writeDocument(GameDocument& document, const QString& path, std::string text,
                       const std::shared_ptr<PgnFile>& newFile);
    void setModified();
    void updateTitles();

    // Clipboard
    void copyPgn();
    void copyFen();
    void paste();
    /// Sends a key to the focused text field, if any. False if no text field has focus.
    static bool forwardToTextField(int key, Qt::KeyboardModifiers modifiers);
    void setUpPosition();
    void openFromText(const QString& text);

    // Showing the game
    void showNode(core::NodeId id, bool animate);
    void showDocument();
    void refreshNotation(); ///< after the tree changed
    void onMoveMade(core::Move move);
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
    void onCommentEdited();
    void onHeaderEdited(const QString& name, const QString& value);

    AppStyle& m_style;
    Library* m_library = nullptr;
    std::vector<std::unique_ptr<GameDocument>> m_documents;

    QTabBar* m_tabs = nullptr;
    QStackedWidget* m_pages = nullptr; ///< 0 = Library, 1 = a game
    LibraryView* m_libraryView = nullptr;
    BoardView* m_board = nullptr;
    NotationView* m_notation = nullptr;
    QPlainTextEdit* m_commentBox = nullptr;
    QLabel* m_commentTitle = nullptr;
    GameInfoPanel* m_info = nullptr;
    QSplitter* m_mainSplitter = nullptr;
    QSplitter* m_sideSplitter = nullptr;
    bool m_defaultSideSplit = false; ///< no saved layout: keep the moves at two thirds
    QWidget* m_forkPanel = nullptr;
    QVBoxLayout* m_forkLayout = nullptr;
    QTimer m_notationRefresh; ///< typing a comment re-renders the notation after a pause
    int m_choice = 0;         ///< chosen continuation at a branch point (index into the children)

    QAction* m_backAction = nullptr;
    QAction* m_forwardAction = nullptr;
    QAction* m_startAction = nullptr;
    QAction* m_endAction = nullptr;
    std::vector<QAction*> m_gameActions; ///< only make sense on a game tab
    std::vector<QShortcut*> m_gameKeys;  ///< ← → ↑ ↓ Home End for the game; off in the Library
    QPushButton* m_backButton = nullptr;
    QPushButton* m_forwardButton = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_endButton = nullptr;
    QActionGroup* m_themeGroup = nullptr;
    QAction* m_customColorsAction = nullptr;
    QActionGroup* m_piecesGroup = nullptr;
    QActionGroup* m_appearanceGroup = nullptr;
};

} // namespace chessgui::app
