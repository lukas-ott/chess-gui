#pragma once

#include "chessgui/core/game.hpp"

#include <QMainWindow>

class QAction;
class QKeySequence;
class QActionGroup;
class QMenu;
class QPushButton;
class QSplitter;
class QTextBrowser;
class QVBoxLayout;

namespace chessgui::app {

class AppStyle;
class BoardView;

/// Fixed layout from the spec: document tabs on top, board on the left, the move list (with
/// navigation buttons) and tabbed panels (Engine, Explorer, Repertoire, Game info) on the right,
/// each in a rounded card.
///
/// Holds the game being analysed. Moves played on the board are added to its tree (as a new
/// variation when they differ from what's there), and drawings are stored on the current move.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(AppStyle& style, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildMenus();
    void bindPlainKey(QAction* action, const QKeySequence& key);
    void buildViewMenu(QMenu* menu);
    void buildLayout();
    [[nodiscard]] QWidget* buildMovesCard();
    [[nodiscard]] static QWidget* buildPanelsCard();
    void restoreState();
    void fitBoardColumn();
    void showAbout();

    /// Shows a node of the game on the board, with its drawings, and highlights it in the list.
    void showNode(core::NodeId id, bool animate);
    void onMoveMade(core::Move move);
    void newGame();
    void goBack();
    void goForward();
    void goToStart();
    void goToEnd();
    void updateNavigation();
    void updateMoveList();
    void updateIcons();
    void updateFork();
    static void markContinuationsLearned();
    /// ↑ / ↓: change the chosen continuation at a branch point, else go to the start / end.
    void stepChoiceOrJump(int step);
    void chooseCustomColors();

    AppStyle& m_style;
    core::Game m_game;
    core::NodeId m_current = core::Game::root();

    BoardView* m_board = nullptr;
    QTextBrowser* m_moveList = nullptr;
    QWidget* m_forkPanel = nullptr;
    QVBoxLayout* m_forkLayout = nullptr;
    int m_choice = 0; ///< chosen continuation at a branch point (index into the children)
    QSplitter* m_mainSplitter = nullptr;
    QSplitter* m_sideSplitter = nullptr;
    QAction* m_backAction = nullptr;
    QAction* m_forwardAction = nullptr;
    QAction* m_startAction = nullptr;
    QAction* m_endAction = nullptr;
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
