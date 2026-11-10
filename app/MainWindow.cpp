#include "MainWindow.hpp"

#include "board/BoardTheme.hpp"
#include "board/BoardView.hpp"
#include "board/PieceSet.hpp"
#include "document/PgnIo.hpp"
#include "library/Library.hpp"
#include "library/LibraryView.hpp"
#include "notation/NotationView.hpp"
#include "panels/GameInfoPanel.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace chessgui::app {
namespace {

using namespace Qt::StringLiterals;
using core::Game;
using core::NodeId;

// QSettings keys (compile-time constants: nothing to construct at startup)
constexpr auto kThemeKey = "board/theme"_L1;
constexpr auto kCustomLightKey = "board/customLight"_L1;
constexpr auto kCustomDarkKey = "board/customDark"_L1;
constexpr auto kPiecesKey = "board/pieces"_L1;
constexpr auto kFlippedKey = "board/flipped"_L1;
constexpr auto kCoordinatesKey = "board/coordinates"_L1;
constexpr auto kCustomTheme = "Custom"_L1;
constexpr auto kAppearanceKey = "app/appearance"_L1;
constexpr auto kContinuationsLearnedKey = "hints/continuationsLearned"_L1;
constexpr auto kLibraryFolderKey = "library/folder"_L1;

constexpr int kStatusMs = 4000;

/// A rounded panel (styled through the "card" property in AppStyle).
QFrame* card() {
    auto* frame = new QFrame;
    frame->setProperty("card", true);
    return frame;
}

QLabel* label(const QString& text, const char* role) {
    auto* result = new QLabel(text);
    result->setProperty("role", role);
    return result;
}

/// What a panel shows until its feature exists: a title and one line about what's coming.
QWidget* emptyState(const QString& title, const QString& hint) {
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->addStretch();
    auto* titleLabel = label(title, "emptyTitle");
    titleLabel->setAlignment(Qt::AlignCenter);
    auto* hintLabel = label(hint, "emptyHint");
    hintLabel->setAlignment(Qt::AlignCenter);
    hintLabel->setWordWrap(true);
    layout->addWidget(titleLabel);
    layout->addWidget(hintLabel);
    layout->addStretch();
    return widget;
}

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// True if `node` is `ancestor` or lies below it.
bool isWithin(const Game& game, NodeId node, NodeId ancestor) {
    for (NodeId current = node; current != core::kNoNode; current = game.parent(current)) {
        if (current == ancestor) {
            return true;
        }
    }
    return false;
}

/// Runs `done` with a future's result on the UI thread once the worker has finished.
/// (The watcher belongs to `context` and deletes itself when done; the analyzer can't see Qt's
/// parent ownership and reports a leak, hence the NOLINT.)
template <typename T, typename Done>
void whenReady(QObject* context, const QFuture<T>& future, const Done& done) {
    auto* watcher =
        new QFutureWatcher<T>(context); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    QObject::connect(watcher, &QFutureWatcher<T>::finished, context, [watcher, done] {
        watcher->deleteLater();
        done(watcher->result());
    });
    watcher->setFuture(future);
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

} // namespace

MainWindow::MainWindow(AppStyle& style, QWidget* parent)
    : QMainWindow(parent), m_style(style), m_library(new Library(this)) {
    setWindowTitle(QStringLiteral("ChessGui"));
    // Smaller than this and the tabs, the game list and the board no longer fit side by side.
    setMinimumSize(960, 620);
    setAcceptDrops(true);
    m_notationRefresh.setSingleShot(true);
    m_notationRefresh.setInterval(300);
    connect(&m_notationRefresh, &QTimer::timeout, this, &MainWindow::refreshNotation);

    buildLayout();
    restoreState(); // before the menus, which show the board's current settings
    buildMenus();
    statusBar()->setSizeGripEnabled(false);

    // The library loads in the background; the window is up before it's done.
    m_library->setFolder(QSettings().value(kLibraryFolderKey, Library::defaultFolder()).toString());

    addDocument(std::make_unique<GameDocument>()); // an empty board to play on right away
    m_tabs->setCurrentIndex(0);                    // but start in the Library
    onTabChanged(0);
}

MainWindow::~MainWindow() = default;

// ---- Menus -------------------------------------------------------------------------------------

void MainWindow::bindPlainKey(QAction* action, const QKeySequence& key) {
    // macOS's menu bar ignores navigation keys (← → Home End) that have no modifier, so set on a
    // menu item they never fire. The window handles these keys itself instead (whatever has
    // focus inside it); the menu item keeps the key only to show it. WidgetShortcut stops the
    // item's own copy from firing too (and from clashing with the window's on other platforms).
    // Not for letters: the macOS menu bar does handle those, so they would fire twice.
    action->setShortcut(key);
    action->setShortcutContext(Qt::WidgetShortcut);
    auto* shortcut = new QShortcut(key, this);
    shortcut->setContext(Qt::WindowShortcut);
    m_gameKeys.push_back(shortcut);
    connect(shortcut, &QShortcut::activated, action, [action] {
        if (action->isEnabled()) {
            action->trigger();
        }
    });
}

void MainWindow::buildMenus() {
    buildFileMenu();
    buildEditMenu();
    buildGameMenu();
    buildViewMenu(menuBar()->addMenu(tr("&View")));
    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAction = helpMenu->addAction(tr("&About ChessGui"), this, &MainWindow::showAbout);
    aboutAction->setMenuRole(QAction::AboutRole);
}

void MainWindow::buildFileMenu() {
    auto* menu = menuBar()->addMenu(tr("&File"));
    menu->addAction(tr("New &Tab"), QKeySequence::AddTab, this,
                    [this] { addDocument(std::make_unique<GameDocument>()); });
    menu->addAction(tr("&Open…"), QKeySequence::Open, this, [this] {
        const QStringList paths = QFileDialog::getOpenFileNames(
            this, tr("Open PGN"), m_library->folder(), tr("PGN files (*.pgn);;All files (*)"));
        for (const auto& path : paths) {
            openFile(path);
        }
    });
    menu->addAction(tr("&Library"), QKeySequence(Qt::CTRL | Qt::Key_L), this,
                    [this] { m_tabs->setCurrentIndex(0); });
    menu->addSeparator();
    auto* saveAction = menu->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::save);
    auto* saveAsAction =
        menu->addAction(tr("Save &As…"), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    auto* closeAction = menu->addAction(tr("&Close Tab"), QKeySequence::Close, this,
                                        [this] { closeTab(m_tabs->currentIndex()); });
    m_gameActions.insert(m_gameActions.end(), {saveAction, saveAsAction, closeAction});
    menu->addSeparator();
    menu->addAction(tr("Change Library Folder…"), this, &MainWindow::chooseLibraryFolder);
    menu->addSeparator();
    auto* quitAction = menu->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    quitAction->setMenuRole(QAction::QuitRole);
}

void MainWindow::buildEditMenu() {
    auto* menu = menuBar()->addMenu(tr("&Edit"));
    auto* copyPgnAction =
        menu->addAction(tr("Copy Game as PGN"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
                        this, &MainWindow::copyPgn);
    auto* copyFenAction =
        menu->addAction(tr("Copy Position as FEN"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C),
                        this, &MainWindow::copyFen);
    menu->addAction(tr("&Paste"), QKeySequence::Paste, this, &MainWindow::paste);
    menu->addSeparator();
    menu->addAction(tr("Set Up Position from FEN…"), this, &MainWindow::setUpPosition);
    m_gameActions.insert(m_gameActions.end(), {copyPgnAction, copyFenAction});
}

void MainWindow::buildGameMenu() {
    auto* menu = menuBar()->addMenu(tr("&Game"));
    m_backAction = menu->addAction(tr("&Back"), this, &MainWindow::goBack);
    bindPlainKey(m_backAction, Qt::Key_Left);
    m_forwardAction = menu->addAction(tr("&Forward"), this, &MainWindow::goForward);
    bindPlainKey(m_forwardAction, Qt::Key_Right);
    m_startAction = menu->addAction(tr("Go to &Start"), this, &MainWindow::goToStart);
    bindPlainKey(m_startAction, Qt::Key_Home);
    m_endAction = menu->addAction(tr("Go to &End"), this, &MainWindow::goToEnd);
    bindPlainKey(m_endAction, Qt::Key_End);

    // ↑ ↓: choose between continuations at a branch point; elsewhere go to the start / end.
    // Plain window shortcuts for the same macOS reason as bindPlainKey().
    auto* up = new QShortcut(Qt::Key_Up, this);
    connect(up, &QShortcut::activated, this, [this] { stepChoiceOrJump(-1); });
    auto* down = new QShortcut(Qt::Key_Down, this);
    connect(down, &QShortcut::activated, this, [this] { stepChoiceOrJump(+1); });
    m_gameKeys.push_back(up);
    m_gameKeys.push_back(down);

    menu->addSeparator();
    // ⌘↑ and ⌘⌫ also mean something in a text field (top of the text, delete to the start of
    // the line). macOS's menu bar takes ⌘ shortcuts first, so while the comment box or another
    // text field has focus they are handed to it instead of changing the game.
    auto* promoteAction = menu->addAction(
        tr("&Promote Variation"), QKeySequence(Qt::CTRL | Qt::Key_Up), this, [this] {
            if (!forwardToTextField(Qt::Key_Up, Qt::ControlModifier)) {
                promote(doc()->current);
            }
        });
    auto* mainLineAction =
        menu->addAction(tr("Make &Main Line"), this, [this] { makeMainLine(doc()->current); });
    auto* deleteFromAction = menu->addAction(
        tr("&Delete From Here"), QKeySequence(Qt::CTRL | Qt::Key_Backspace), this, [this] {
            if (!forwardToTextField(Qt::Key_Backspace, Qt::ControlModifier)) {
                deleteFrom(doc()->current);
            }
        });
    auto* deleteVariationAction =
        menu->addAction(tr("Delete &Variation"), this, [this] { deleteVariation(doc()->current); });
    menu->addSeparator();
    auto* commentAction = menu->addAction(tr("Edit &Comment"), QKeySequence(Qt::CTRL | Qt::Key_E),
                                          this, [this] { editComment(doc()->current); });
    m_gameActions.insert(m_gameActions.end(),
                         {m_backAction, m_forwardAction, m_startAction, m_endAction, promoteAction,
                          mainLineAction, deleteFromAction, deleteVariationAction, commentAction});
}

void MainWindow::buildViewMenu(QMenu* menu) {
    // Appearance of the whole app: follow macOS, or always light / dark.
    auto* appearanceMenu = menu->addMenu(tr("&Appearance"));
    m_appearanceGroup = new QActionGroup(this);
    for (const auto& [appearance, text] :
         {std::pair{AppStyle::Appearance::System, tr("Same as System")},
          std::pair{AppStyle::Appearance::Light, tr("Light")},
          std::pair{AppStyle::Appearance::Dark, tr("Dark")}}) {
        auto* action = appearanceMenu->addAction(text);
        action->setCheckable(true);
        action->setChecked(m_style.appearance() == appearance);
        m_appearanceGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, appearance] {
            m_style.setAppearance(appearance);
            QSettings().setValue(kAppearanceKey, AppStyle::toString(appearance));
        });
    }
    menu->addSeparator();

    auto* flipAction = menu->addAction(tr("&Flip Board"));
    flipAction->setShortcut(Qt::Key_F); // letters work in the macOS menu bar (unlike arrows)
    flipAction->setCheckable(true);
    flipAction->setChecked(m_board->isFlipped());
    connect(flipAction, &QAction::toggled, this, [this](bool on) {
        m_board->setFlipped(on);
        QSettings().setValue(kFlippedKey, on);
    });

    auto* coordinatesAction = menu->addAction(tr("Show &Coordinates"));
    coordinatesAction->setCheckable(true);
    coordinatesAction->setChecked(m_board->showCoordinates());
    connect(coordinatesAction, &QAction::toggled, this, [this](bool on) {
        m_board->setShowCoordinates(on);
        QSettings().setValue(kCoordinatesKey, on);
    });
    menu->addSeparator();

    // Board colours: presets plus custom colours.
    auto* themeMenu = menu->addMenu(tr("Board &Colours"));
    m_themeGroup = new QActionGroup(this);
    const QString current = m_board->theme().name;
    for (const auto& theme : BoardTheme::presets()) {
        auto* action = themeMenu->addAction(theme.name);
        action->setCheckable(true);
        action->setChecked(theme.name == current);
        m_themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, theme] {
            m_board->setTheme(theme);
            m_libraryView->setBoardLook(theme, m_board->pieceSetName());
            QSettings().setValue(kThemeKey, theme.name);
        });
    }
    themeMenu->addSeparator();
    m_customColorsAction = themeMenu->addAction(tr("Custom Colours…"));
    m_customColorsAction->setCheckable(true);
    m_customColorsAction->setChecked(current == kCustomTheme);
    m_themeGroup->addAction(m_customColorsAction);
    connect(m_customColorsAction, &QAction::triggered, this, &MainWindow::chooseCustomColors);

    // Piece sets
    auto* piecesMenu = menu->addMenu(tr("&Pieces"));
    m_piecesGroup = new QActionGroup(this);
    for (const auto& name : PieceSet::available()) {
        auto* action = piecesMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(name == m_board->pieceSetName());
        m_piecesGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, name] {
            m_board->setPieceSet(name);
            m_libraryView->setBoardLook(m_board->theme(), name);
            QSettings().setValue(kPiecesKey, name);
        });
    }
}

void MainWindow::chooseCustomColors() {
    const BoardTheme now = m_board->theme();
    const QColor light = QColorDialog::getColor(now.light, this, tr("Light Squares"));
    const QColor dark =
        light.isValid() ? QColorDialog::getColor(now.dark, this, tr("Dark Squares")) : QColor();
    if (!light.isValid() || !dark.isValid()) {
        // Cancelled: tick the theme that is still in use.
        for (auto* action : m_themeGroup->actions()) {
            const bool inUse = now.name == kCustomTheme ? action == m_customColorsAction
                                                        : action->text() == now.name;
            action->setChecked(inUse);
        }
        return;
    }
    m_board->setTheme(BoardTheme::custom(light, dark));
    m_libraryView->setBoardLook(m_board->theme(), m_board->pieceSetName());
    QSettings settings;
    settings.setValue(kThemeKey, kCustomTheme);
    settings.setValue(kCustomLightKey, light);
    settings.setValue(kCustomDarkKey, dark);
}

void MainWindow::chooseLibraryFolder() {
    const QString folder =
        QFileDialog::getExistingDirectory(this, tr("Library Folder"), m_library->folder());
    if (!folder.isEmpty()) {
        m_library->setFolder(folder);
        QSettings().setValue(kLibraryFolderKey, folder);
    }
}

// ---- Layout ------------------------------------------------------------------------------------

void MainWindow::buildLayout() {
    m_tabs = new QTabBar;
    m_tabs->setObjectName(QStringLiteral("documentTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->setTabsClosable(true);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    m_tabs->setElideMode(Qt::ElideRight);
    m_tabs->addTab(tr("Library"));
    // The Library tab can't be closed.
    m_tabs->setTabButton(0, QTabBar::LeftSide, nullptr);
    m_tabs->setTabButton(0, QTabBar::RightSide, nullptr);
    connect(m_tabs, &QTabBar::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_tabs, &QTabBar::tabCloseRequested, this, &MainWindow::closeTab);

    auto* newTab = new QToolButton;
    newTab->setObjectName(QStringLiteral("newTabButton"));
    newTab->setText(QStringLiteral("+"));
    newTab->setToolTip(tr("New tab (⌘T)"));
    newTab->setFocusPolicy(Qt::NoFocus);
    connect(newTab, &QToolButton::clicked, this,
            [this] { addDocument(std::make_unique<GameDocument>()); });
    auto* tabRow = new QWidget;
    tabRow->setObjectName(QStringLiteral("tabRow"));
    auto* tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(0, 0, 8, 0);
    tabLayout->setSpacing(4);
    tabLayout->addWidget(m_tabs);
    tabLayout->addWidget(newTab);
    tabLayout->addStretch();

    m_libraryView = new LibraryView(*m_library, m_style);
    connect(m_libraryView, &LibraryView::openGameRequested, this, &MainWindow::openGame);
    connect(m_libraryView, &LibraryView::changeFolderRequested, this,
            &MainWindow::chooseLibraryFolder);

    m_pages = new QStackedWidget;
    m_pages->addWidget(m_libraryView);
    m_pages->addWidget(buildAnalysisPage());

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(tabRow);
    layout->addWidget(m_pages, 1);
    setCentralWidget(central);

    connect(&m_style, &AppStyle::changed, this, [this] {
        updateIcons();
        refreshNotation();
    });
    updateIcons();
}

QWidget* MainWindow::buildAnalysisPage() {
    m_board = new BoardView;
    connect(m_board, &BoardView::moveMade, this, &MainWindow::onMoveMade);
    connect(
        m_board, &BoardView::drawingsChanged, this,
        [this](const std::vector<core::Arrow>& arrows, const std::vector<core::SquareMark>& marks) {
            if (auto* d = doc()) {
                d->game.setArrows(d->current, arrows);
                d->game.setSquareMarks(d->current, marks);
                setModified();
            }
        });

    m_sideSplitter = new QSplitter(Qt::Vertical);
    m_sideSplitter->addWidget(buildMovesCard());
    m_sideSplitter->addWidget(buildPanelsCard());
    m_sideSplitter->setChildrenCollapsible(false);
    m_sideSplitter->setStretchFactor(0, 2);
    m_sideSplitter->setStretchFactor(1, 1);

    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->addWidget(m_board);
    m_mainSplitter->addWidget(m_sideSplitter);
    m_mainSplitter->setChildrenCollapsible(false);
    // When the window grows, the board gets the extra room; the side column keeps its width.
    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 0);
    m_sideSplitter->setMinimumWidth(440);

    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 4, 10, 10);
    layout->addWidget(m_mainSplitter);
    return page;
}

QWidget* MainWindow::buildMovesCard() {
    auto* frame = card();
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    layout->addWidget(label(tr("MOVES"), "cardTitle"));

    m_notation = new NotationView(m_style);
    connect(m_notation, &NotationView::nodeActivated, this,
            [this](NodeId id) { showNode(id, false); });
    connect(m_notation, &NotationView::promoteRequested, this, &MainWindow::promote);
    connect(m_notation, &NotationView::makeMainLineRequested, this, &MainWindow::makeMainLine);
    connect(m_notation, &NotationView::deleteFromRequested, this, &MainWindow::deleteFrom);
    connect(m_notation, &NotationView::deleteVariationRequested, this,
            &MainWindow::deleteVariation);
    connect(m_notation, &NotationView::nagToggled, this, &MainWindow::toggleNag);
    connect(m_notation, &NotationView::nagsCleared, this, &MainWindow::clearNags);
    connect(m_notation, &NotationView::commentEditRequested, this, &MainWindow::editComment);
    m_notation->setMinimumHeight(200); // always room for a good few lines of moves
    layout->addWidget(m_notation, 1);

    // The current move's comment, edited in place, under a header naming the move.
    m_commentTitle = label(QString(), "cardTitle");
    layout->addWidget(m_commentTitle);
    m_commentBox = new QPlainTextEdit;
    m_commentBox->setObjectName(QStringLiteral("commentBox"));
    m_commentBox->setTabChangesFocus(true);
    m_commentBox->setFixedHeight(58);
    connect(m_commentBox, &QPlainTextEdit::textChanged, this, &MainWindow::onCommentEdited);
    layout->addWidget(m_commentBox);

    // At a branch point: the continuations to choose from (↑ ↓ to choose, → to play).
    m_forkPanel = new QWidget;
    m_forkLayout = new QVBoxLayout(m_forkPanel);
    m_forkLayout->setContentsMargins(0, 0, 0, 0);
    m_forkLayout->setSpacing(4);
    m_forkPanel->hide();
    layout->addWidget(m_forkPanel);

    // Start / back / forward / end; each mirrors a Game menu action. Icons: updateIcons().
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    const auto addButton = [&](const QString& tip, void (MainWindow::*slot)()) {
        auto* button = new QPushButton;
        button->setIconSize(QSize(16, 16));
        button->setProperty("role", "nav");
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this, slot);
        buttons->addWidget(button);
        return button;
    };
    m_startButton = addButton(tr("Start (Home)"), &MainWindow::goToStart);
    m_backButton = addButton(tr("Back (←)"), &MainWindow::goBack);
    m_forwardButton = addButton(tr("Forward (→)"), &MainWindow::goForward);
    m_endButton = addButton(tr("End (End)"), &MainWindow::goToEnd);
    layout->addLayout(buttons);
    return frame;
}

QWidget* MainWindow::buildPanelsCard() {
    auto* frame = card();
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(6, 2, 6, 6);

    auto* panels = new QTabWidget;
    panels->setObjectName(QStringLiteral("panels"));
    panels->setDocumentMode(true);
    panels->setFocusPolicy(Qt::NoFocus);
    panels->tabBar()->setFocusPolicy(Qt::NoFocus);
    panels->tabBar()->setDrawBase(false);
    panels->addTab(emptyState(tr("Engine analysis"),
                              tr("Coming in M4: Stockfish or any UCI engine, several lines at "
                                 "once, eval bar and best-move arrow.")),
                   tr("Engine"));
    panels->addTab(emptyState(tr("Opening explorer"),
                              tr("Coming in v2: what masters and Lichess players play here, "
                                 "and how it scores.")),
                   tr("Explorer"));
    panels->addTab(emptyState(tr("Repertoire"),
                              tr("Coming in v2: build your lines, find the gaps, train them.")),
                   tr("Repertoire"));
    m_info = new GameInfoPanel;
    connect(m_info, &GameInfoPanel::headerEdited, this, &MainWindow::onHeaderEdited);
    // The game details scroll when the panel is short, so they never take height from the moves.
    auto* infoScroll = new QScrollArea;
    infoScroll->setObjectName(QStringLiteral("infoScroll"));
    infoScroll->setWidget(m_info);
    infoScroll->setWidgetResizable(true);
    infoScroll->setFrameShape(QFrame::NoFrame);
    infoScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    infoScroll->viewport()->setAutoFillBackground(false);
    m_info->setAutoFillBackground(false);
    panels->addTab(infoScroll, tr("Info"));
    layout->addWidget(panels);
    frame->setMinimumHeight(150);
    return frame;
}

void MainWindow::restoreState() {
    const QSettings settings;
    if (!restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray())) {
        // First launch: fill the screen (the Dock and menu bar stay), like ⌥-clicking the
        // green button; not full screen. The size below is what un-zooming goes back to.
        resize(1200, 780);
        setWindowState(windowState() | Qt::WindowMaximized);
    }
    m_mainSplitter->restoreState(
        settings.value(QStringLiteral("window/mainSplitter")).toByteArray());
    if (!m_sideSplitter->restoreState(
            settings.value(QStringLiteral("window/sideSplitter")).toByteArray())) {
        m_defaultSideSplit = true; // until the user drags the divider
    }
    connect(m_sideSplitter, &QSplitter::splitterMoved, this,
            [this] { m_defaultSideSplit = false; });

    const QString theme = settings.value(kThemeKey).toString();
    if (theme == kCustomTheme) {
        m_board->setTheme(BoardTheme::custom(settings.value(kCustomLightKey).value<QColor>(),
                                             settings.value(kCustomDarkKey).value<QColor>()));
    } else {
        m_board->setTheme(BoardTheme::preset(theme));
    }
    m_board->setPieceSet(settings.value(kPiecesKey).toString());
    m_board->setFlipped(settings.value(kFlippedKey, false).toBool());
    m_board->setShowCoordinates(settings.value(kCoordinatesKey, true).toBool());
    m_libraryView->setBoardLook(m_board->theme(), m_board->pieceSetName());
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    fitBoardColumn();
    // Once more when the layout has settled: right now the splitters still have their old size.
    QMetaObject::invokeMethod(this, &MainWindow::fitBoardColumn, Qt::QueuedConnection);
}

void MainWindow::fitBoardColumn() {
    // The board is square, so a board column wider than it is tall only adds empty space.
    // Give it exactly its height and the rest to the side column.
    const QList<int> sizes = m_mainSplitter->sizes();
    if (sizes.size() != 2 || m_mainSplitter->height() <= 0) {
        return;
    }
    const int total = sizes[0] + sizes[1];
    const int side = std::max(m_sideSplitter->minimumWidth(), total - m_mainSplitter->height());
    m_mainSplitter->setSizes({total - side, side});
    if (m_defaultSideSplit) {
        // The moves are what you read: they get two thirds of the height, the panels the rest.
        const int height = m_mainSplitter->height();
        const int moves = (height * 2) / 3;
        m_sideSplitter->setSizes({moves, height - moves});
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    for (const auto& document : m_documents) {
        if (!maybeSave(*document)) {
            event->ignore();
            return;
        }
    }
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/mainSplitter"), m_mainSplitter->saveState());
    settings.setValue(QStringLiteral("window/sideSplitter"), m_sideSplitter->saveState());
    event->accept();
}

void MainWindow::showAbout() {
    QMessageBox::about(this, tr("About ChessGui"),
                       tr("<b>ChessGui %1</b><p>A fast native chess GUI for opening preparation."
                          "<p>Licensed under the GNU GPL v3. Piece sets: cburnett and merida "
                          "(GPLv2+), chessnut (Apache 2.0), fantasy (MIT), via Lichess.")
                           .arg(QApplication::applicationVersion()));
}

// ---- Documents and tabs ------------------------------------------------------------------------

GameDocument* MainWindow::docAt(int tab) const {
    if (tab <= 0) { // the Library
        return nullptr;
    }
    // Each game tab remembers its document's id (tabs can be closed in any order).
    const int id = m_tabs->tabData(tab).toInt();
    for (const auto& document : m_documents) {
        if (document->id == id) {
            return document.get();
        }
    }
    return nullptr;
}

GameDocument* MainWindow::doc() const {
    return docAt(m_tabs->currentIndex());
}

void MainWindow::addDocument(std::unique_ptr<GameDocument> document) {
    // An untouched empty game is replaced rather than left lying around in its own tab.
    for (int tab = 1; tab < m_tabs->count(); ++tab) {
        if (GameDocument* existing = docAt(tab); existing->isPristine()) {
            const int id = existing->id; // the tab knows the document by this id: keep it
            *existing = std::move(*document);
            existing->id = id;
            if (m_tabs->currentIndex() == tab) {
                onTabChanged(tab);
            } else {
                m_tabs->setCurrentIndex(tab);
            }
            updateTitles();
            return;
        }
    }
    const GameDocument* raw = document.get();
    m_documents.push_back(std::move(document));
    const int tab = m_tabs->addTab(raw->title());
    m_tabs->setTabData(tab, raw->id);
    // Our own close button (drawn icon) instead of the platform's, on the right.
    auto* close = new QToolButton;
    close->setObjectName(QStringLiteral("tabClose"));
    close->setIconSize(QSize(12, 12));
    close->setFocusPolicy(Qt::NoFocus);
    close->setToolTip(tr("Close tab (⌘W)"));
    const auto& c = m_style.colors();
    close->setIcon(makeIcon(IconKind::Close, c.muted, c.dimmed, 12));
    connect(close, &QToolButton::clicked, this, [this, close] {
        for (int i = 1; i < m_tabs->count(); ++i) {
            if (m_tabs->tabButton(i, QTabBar::RightSide) == close) {
                closeTab(i);
                return;
            }
        }
    });
    m_tabs->setTabButton(tab, QTabBar::LeftSide, nullptr);
    m_tabs->setTabButton(tab, QTabBar::RightSide, close);
    m_tabs->setCurrentIndex(tab);
    updateTitles();
}

void MainWindow::onTabChanged(int tab) {
    m_notationRefresh.stop();
    const GameDocument* d = docAt(tab);
    for (auto* action : m_gameActions) {
        action->setEnabled(d != nullptr);
    }
    // The arrow keys move through the game only on a game tab; in the Library they belong to
    // the file tree and the game list.
    for (auto* key : m_gameKeys) {
        key->setEnabled(d != nullptr);
    }
    if (d == nullptr) {
        m_notation->setGame(nullptr, Game::root());
        m_info->setGame(nullptr);
        m_pages->setCurrentIndex(0);
    } else {
        m_pages->setCurrentIndex(1);
        showDocument();
        m_board->setFocus();
        fitBoardColumn();
    }
    updateTitles();
}

void MainWindow::closeTab(int tab) {
    GameDocument* d = docAt(tab);
    if (d == nullptr || !maybeSave(*d)) {
        return;
    }
    m_tabs->removeTab(tab); // switches tabs first, so nothing shows the game being removed
    std::erase_if(m_documents, [d](const auto& document) { return document.get() == d; });
    updateTitles();
}

bool MainWindow::maybeSave(GameDocument& document) {
    if (!document.modified) {
        return true;
    }
    for (int tab = 1; tab < m_tabs->count(); ++tab) {
        if (docAt(tab) == &document) {
            m_tabs->setCurrentIndex(tab); // show which game the question is about
        }
    }
    const auto answer = QMessageBox::question(
        this, tr("Save changes?"),
        tr("Do you want to save the changes to “%1”?").arg(document.title().mid(2)),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Discard) {
        return true;
    }
    if (answer == QMessageBox::Cancel) {
        return false;
    }
    // Save now and wait: the window is about to close the game.
    const bool inFile = document.file && !document.file->path.isEmpty() && document.gameIndex >= 0;
    QString path = inFile ? document.file->path : QString();
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, tr("Save Game"),
            QDir(m_library->folder()).filePath(document.title().mid(2) + u".pgn"),
            tr("PGN files (*.pgn)"));
        if (path.isEmpty()) {
            return false;
        }
    }
    const std::string gameText = pgn::writeGame(document.game);
    const std::string text = inFile && path == document.file->path
                                 ? pgnio::replaceGame(*document.file, document.gameIndex, gameText)
                                 : gameText;
    const QString error = pgnio::write(path, text).result();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Couldn't save"), error);
        return false;
    }
    document.modified = false;
    return true;
}

void MainWindow::save() {
    GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    if (!d->file || d->file->path.isEmpty() || d->gameIndex < 0) {
        saveAs();
        return;
    }
    std::string text = pgnio::replaceGame(*d->file, d->gameIndex, pgn::writeGame(d->game));
    writeDocument(*d, d->file->path, std::move(text), nullptr);
}

void MainWindow::saveAs() {
    const GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    const QString name = d->title().startsWith(u"• ") ? d->title().mid(2) : d->title();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Game"), QDir(m_library->folder()).filePath(name + u".pgn"),
        tr("PGN files (*.pgn)"));
    if (!path.isEmpty()) {
        saveCurrentAs(path);
    }
}

void MainWindow::saveCurrentAs(const QString& path) {
    GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    // A new file with just this game.
    const auto file = std::make_shared<PgnFile>();
    file->path = path;
    file->text = pgn::writeGame(d->game);
    file->games = pgn::indexGames(file->text);
    std::string text = file->text;
    writeDocument(*d, path, std::move(text), file);
}

void MainWindow::writeDocument(GameDocument& document, const QString& path, std::string text,
                               const std::shared_ptr<PgnFile>& newFile) {
    GameDocument* target = &document;
    const auto shared = std::make_shared<std::string>(text);
    whenReady(
        this, pgnio::write(path, std::move(text)),
        [this, target, path, shared, newFile](const QString& error) {
            // The tab may have been closed while the file was being written.
            const bool alive = std::any_of(m_documents.begin(), m_documents.end(),
                                           [target](const auto& d) { return d.get() == target; });
            if (!error.isEmpty()) {
                QMessageBox::warning(this, tr("Couldn't save"), error);
                emit fileError(error);
                return;
            }
            if (alive) {
                if (newFile) {
                    target->file = newFile;
                    target->gameIndex = 0;
                } else {
                    target->file->text = *shared;
                    target->file->games = pgn::indexGames(target->file->text);
                }
                target->modified = false;
                updateTitles();
            }
            statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), kStatusMs);
            emit fileSaved(path);
        });
}

void MainWindow::setModified() {
    if (GameDocument* d = doc(); d != nullptr && !d->modified) {
        d->modified = true;
        updateTitles();
    }
}

void MainWindow::updateTitles() {
    for (int tab = 1; tab < m_tabs->count(); ++tab) {
        if (const GameDocument* d = docAt(tab)) {
            // A long title (opening files have them) is cut off; hovering shows all of it.
            const QString title = d->title();
            const QString shown = m_tabs->fontMetrics().elidedText(title, Qt::ElideRight, 240);
            m_tabs->setTabText(tab, shown);
            m_tabs->setTabToolTip(tab, shown == title ? QString() : title);
        }
    }
    const GameDocument* d = doc();
    if (d == nullptr) {
        setWindowTitle(tr("Library — ChessGui"));
        setWindowModified(false);
        return;
    }
    const QString title = d->modified ? d->title().mid(2) : d->title();
    setWindowTitle(title + QStringLiteral("[*] — ChessGui"));
    setWindowModified(d->modified);
}

// ---- Opening -----------------------------------------------------------------------------------

void MainWindow::openFile(const QString& path) {
    statusBar()->showMessage(tr("Opening %1…").arg(QFileInfo(path).fileName()));
    whenReady(this, pgnio::load(path), [this, path](const pgnio::Loaded& loaded) {
        statusBar()->clearMessage();
        if (!loaded.file) {
            QMessageBox::warning(this, tr("Couldn't open"), loaded.error);
            emit fileError(loaded.error);
            return;
        }
        if (loaded.file->games.empty()) {
            const QString message = tr("%1 contains no games.").arg(QFileInfo(path).fileName());
            QMessageBox::information(this, tr("No games"), message);
            emit fileError(message);
            return;
        }
        int index = 0;
        if (loaded.file->games.size() > 1) {
            // A file with several games: let the user pick one.
            QStringList items;
            for (std::size_t i = 0; i < loaded.file->games.size(); ++i) {
                const auto& game = loaded.file->games[i];
                items << QStringLiteral("%1.  %2 – %3   %4")
                             .arg(i + 1)
                             .arg(toQString(game.header("White")), toQString(game.header("Black")),
                                  toQString(game.header("Result")));
            }
            bool ok = false;
            const QString choice = QInputDialog::getItem(
                this, QFileInfo(path).fileName(),
                tr("This file has %n game(s). Which one?", nullptr, static_cast<int>(items.size())),
                items, 0, false, &ok);
            if (!ok) {
                return;
            }
            index = static_cast<int>(items.indexOf(choice));
        }
        if (index == 0 && !loaded.first) {
            QMessageBox::warning(this, tr("Couldn't read the game"), loaded.error);
            emit fileError(loaded.error);
            return;
        }
        if (index == 0) {
            auto document = std::make_unique<GameDocument>();
            document->game = *loaded.first;
            document->file = loaded.file;
            document->gameIndex = 0;
            addDocument(std::move(document));
            emit fileOpened(path);
        } else {
            openGame(loaded.file, index);
        }
    });
}

void MainWindow::openGame(const std::shared_ptr<PgnFile>& file, int index) {
    // Already open? Go there.
    for (int tab = 1; tab < m_tabs->count(); ++tab) {
        const GameDocument* d = docAt(tab);
        if (d != nullptr && d->file && !file->path.isEmpty() && d->file->path == file->path &&
            d->gameIndex == index) {
            m_tabs->setCurrentIndex(tab);
            return;
        }
    }
    whenReady(this, pgnio::parse(file, index), [this, file, index](const pgnio::Parsed& parsed) {
        if (!parsed.game) {
            QMessageBox::warning(this, tr("Couldn't read the game"), parsed.error);
            emit fileError(parsed.error);
            return;
        }
        auto document = std::make_unique<GameDocument>();
        document->game = *parsed.game;
        document->file = file;
        document->gameIndex = index;
        addDocument(std::move(document));
        emit fileOpened(file->path);
    });
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (std::any_of(urls.begin(), urls.end(), [](const QUrl& url) {
            return url.isLocalFile() && url.toLocalFile().endsWith(u".pgn", Qt::CaseInsensitive);
        })) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && url.toLocalFile().endsWith(u".pgn", Qt::CaseInsensitive)) {
            openFile(url.toLocalFile());
        }
    }
    event->acceptProposedAction();
}

// ---- Clipboard ---------------------------------------------------------------------------------

void MainWindow::copyPgn() {
    if (const GameDocument* d = doc()) {
        QApplication::clipboard()->setText(QString::fromStdString(pgn::writeGame(d->game)));
        statusBar()->showMessage(tr("Game copied as PGN"), kStatusMs);
    }
}

void MainWindow::copyFen() {
    if (const GameDocument* d = doc()) {
        QApplication::clipboard()->setText(
            QString::fromStdString(d->game.positionAt(d->current).fen()));
        statusBar()->showMessage(tr("Position copied as FEN"), kStatusMs);
    }
}

bool MainWindow::forwardToTextField(int key, Qt::KeyboardModifiers /*modifiers*/) {
    // Does what the key does in a text field. (Re-sending the key itself would go through the
    // shortcuts again and come straight back here.) Only ⌘⌫ and ⌘↑ are routed through this.
    QWidget* focus = QApplication::focusWidget();
    if (auto* text = qobject_cast<QPlainTextEdit*>(focus)) {
        QTextCursor cursor = text->textCursor();
        if (key == Qt::Key_Backspace) { // delete to the start of the line
            cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
        } else { // ⌘↑: to the top
            cursor.movePosition(QTextCursor::Start);
        }
        text->setTextCursor(cursor);
        return true;
    }
    if (auto* line = qobject_cast<QLineEdit*>(focus)) {
        if (key == Qt::Key_Backspace) {
            line->home(true);
            line->del();
        } else {
            line->home(false);
        }
        return true;
    }
    return false;
}

void MainWindow::paste() {
    // In a text field, ⌘V pastes text as usual.
    QWidget* focus = QApplication::focusWidget();
    if (auto* line = qobject_cast<QLineEdit*>(focus)) {
        line->paste();
        return;
    }
    if (auto* text = qobject_cast<QPlainTextEdit*>(focus)) {
        text->paste();
        return;
    }
    openFromText(QApplication::clipboard()->text());
}

void MainWindow::openFromText(const QString& text) {
    const std::string trimmed = text.trimmed().toStdString();
    if (trimmed.empty()) {
        return;
    }
    // A FEN: a new game from that position.
    if (auto position = core::Position::fromFen(trimmed)) {
        auto document = std::make_unique<GameDocument>();
        document->game = Game(std::move(*position));
        addDocument(std::move(document));
        return;
    }
    // Otherwise PGN: open its first game.
    whenReady(this, pgnio::loadText(trimmed), [this](const pgnio::Loaded& loaded) {
        if (!loaded.first) {
            const QString message = loaded.error.isEmpty()
                                        ? tr("The clipboard holds neither a FEN nor a PGN game.")
                                        : loaded.error;
            statusBar()->showMessage(message, kStatusMs);
            emit fileError(message);
            return;
        }
        auto document = std::make_unique<GameDocument>();
        document->game = *loaded.first;
        document->modified = true; // pasted: not saved anywhere yet
        addDocument(std::move(document));
    });
}

void MainWindow::setUpPosition() {
    bool ok = false;
    const QString fen = QInputDialog::getText(this, tr("Set Up Position"), tr("FEN:"),
                                              QLineEdit::Normal, QString(), &ok);
    if (!ok || fen.trimmed().isEmpty()) {
        return;
    }
    auto position = core::Position::fromFen(fen.trimmed().toStdString());
    if (!position) {
        QMessageBox::warning(this, tr("Invalid FEN"),
                             QString::fromStdString(position.error().message));
        return;
    }
    auto document = std::make_unique<GameDocument>();
    document->game = Game(std::move(*position));
    addDocument(std::move(document));
}

// ---- Showing the game --------------------------------------------------------------------------

void MainWindow::showDocument() {
    const GameDocument* d = doc();
    m_notation->setGame(&d->game, d->current);
    m_info->setGame(&d->game);
    showNode(d->current, false);
}

void MainWindow::refreshNotation() {
    m_notationRefresh.stop();
    if (const GameDocument* d = doc()) {
        m_notation->setGame(&d->game, d->current);
    }
}

void MainWindow::showNode(NodeId id, bool animate) {
    GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    d->current = id;
    const core::Node& node = d->game.node(id);
    const auto lastMove = id == Game::root() ? std::nullopt : std::optional(node.move);
    m_board->setPosition(d->game.positionAt(id), lastMove, animate);
    m_board->setDrawings(node.arrows, node.squareMarks);
    m_notation->setCurrent(id);
    {
        const QSignalBlocker block(m_commentBox);
        m_commentBox->setPlainText(QString::fromStdString(node.comment));
        m_commentBox->setPlaceholderText(id == Game::root() ? tr("Add a comment about the game…")
                                                            : tr("Add a comment about this move…"));
    }
    {
        // "COMMENT · 12… Nf6": which move the box belongs to.
        QString move = tr("GAME");
        if (id != Game::root()) {
            const bool white = d->game.mover(id) == core::Color::White;
            move = QStringLiteral("%1%2 %3")
                       .arg(d->game.moveNumber(id))
                       .arg(white ? QStringLiteral(".") : QStringLiteral("…"))
                       .arg(QString::fromStdString(node.san));
        }
        m_commentTitle->setText(tr("COMMENT") + QStringLiteral("  ·  ") + move);
    }
    m_choice = 0; // the main continuation, until the user picks another
    updateFork();
    updateNavigation();
}

void MainWindow::onMoveMade(core::Move move) {
    GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    const std::size_t before = d->game.nodeCount();
    const NodeId id = d->game.addMove(d->current, move);
    showNode(id, true);
    if (d->game.nodeCount() != before) { // a new move, not one that was already there
        setModified();
        // Board first (spec: a move shows within one frame), then the notation, which can take
        // ~0.1 s to rebuild for a repertoire with thousands of commented positions.
        m_board->repaint();
        refreshNotation();
    }
}

void MainWindow::goBack() {
    if (const GameDocument* d = doc(); d != nullptr && d->current != Game::root()) {
        showNode(d->game.parent(d->current), false);
    }
}

void MainWindow::goForward() {
    const GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    // Follows the chosen continuation at a branch point (the main line otherwise).
    const auto children = d->game.children(d->current);
    if (children.size() > 1) {
        markContinuationsLearned();
    }
    if (!children.empty()) {
        const auto index =
            std::min(static_cast<std::size_t>(std::max(m_choice, 0)), children.size() - 1);
        showNode(children[index], true);
    }
}

void MainWindow::goToStart() {
    if (doc() != nullptr) {
        showNode(Game::root(), false);
    }
}

void MainWindow::goToEnd() {
    const GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    NodeId last = d->current;
    while (d->game.mainChild(last) != core::kNoNode) {
        last = d->game.mainChild(last);
    }
    showNode(last, false);
}

void MainWindow::stepChoiceOrJump(int step) {
    const GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    const auto children = d->game.children(d->current);
    if (children.size() < 2) {
        if (step < 0) {
            goToStart();
        } else {
            goToEnd();
        }
        return;
    }
    const int count = static_cast<int>(children.size());
    m_choice = (m_choice + step + count) % count; // wraps around
    markContinuationsLearned();
    updateFork();
}

void MainWindow::markContinuationsLearned() {
    QSettings().setValue(kContinuationsLearnedKey, true);
}

void MainWindow::updateFork() {
    const GameDocument* d = doc();
    const auto children = d != nullptr ? d->game.children(d->current) : std::vector<NodeId>{};
    // Rebuild the list (it's at most a handful of buttons).
    while (const auto* item = m_forkLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (children.size() < 2) {
        m_forkPanel->hide();
        m_board->setCandidates({}, -1);
        return;
    }
    // A one-time hint: once the user has picked a continuation, the arrows and the
    // highlighted choice explain themselves (as on Lichess), so it never shows again.
    if (!QSettings().value(kContinuationsLearnedKey, false).toBool()) {
        m_forkLayout->addWidget(label(tr("↑ ↓ to choose a continuation, → to play it"), "hint"));
    }
    std::vector<core::Move> moves;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const NodeId child = children[i];
        moves.push_back(d->game.node(child).move);
        const bool white = d->game.mover(child) == core::Color::White;
        auto* button = new QPushButton(QStringLiteral("%1%2 %3")
                                           .arg(d->game.moveNumber(child))
                                           .arg(white ? QStringLiteral(".") : QStringLiteral("…"))
                                           .arg(QString::fromStdString(d->game.node(child).san)));
        button->setProperty("role", "fork");
        button->setProperty("chosen", std::cmp_equal(i, m_choice));
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this, [this, child] {
            markContinuationsLearned();
            showNode(child, true);
        });
        m_forkLayout->addWidget(button);
    }
    m_forkPanel->show();
    m_board->setCandidates(std::move(moves), m_choice);
}

void MainWindow::updateNavigation() {
    const GameDocument* d = doc();
    const bool atStart = d == nullptr || d->current == Game::root();
    const bool atEnd = d == nullptr || d->game.mainChild(d->current) == core::kNoNode;
    for (auto* action : {m_backAction, m_startAction}) {
        action->setEnabled(!atStart);
    }
    for (auto* action : {m_forwardAction, m_endAction}) {
        action->setEnabled(!atEnd);
    }
    m_backButton->setEnabled(!atStart);
    m_startButton->setEnabled(!atStart);
    m_forwardButton->setEnabled(!atEnd);
    m_endButton->setEnabled(!atEnd);
}

void MainWindow::updateIcons() {
    const auto& c = m_style.colors();
    for (int tab = 1; tab < m_tabs->count(); ++tab) {
        if (auto* close = qobject_cast<QToolButton*>(m_tabs->tabButton(tab, QTabBar::RightSide))) {
            close->setIcon(makeIcon(IconKind::Close, c.muted, c.dimmed, 12));
        }
    }
    m_startButton->setIcon(makeIcon(IconKind::First, c.text, c.dimmed));
    m_backButton->setIcon(makeIcon(IconKind::Previous, c.text, c.dimmed));
    m_forwardButton->setIcon(makeIcon(IconKind::Next, c.text, c.dimmed));
    m_endButton->setIcon(makeIcon(IconKind::Last, c.text, c.dimmed));
}

// ---- Editing the tree --------------------------------------------------------------------------

void MainWindow::promote(NodeId id) {
    GameDocument* d = doc();
    if (d == nullptr || id == Game::root() || d->game.mainChild(d->game.parent(id)) == id) {
        return;
    }
    d->game.promote(id);
    setModified();
    refreshNotation();
    showNode(d->current, false);
}

void MainWindow::makeMainLine(NodeId id) {
    GameDocument* d = doc();
    if (d == nullptr || d->game.isMainLine(id)) {
        return;
    }
    d->game.makeMainLine(id);
    setModified();
    refreshNotation();
    showNode(d->current, false);
}

void MainWindow::deleteFrom(NodeId id) {
    GameDocument* d = doc();
    if (d == nullptr || id == Game::root()) {
        return;
    }
    const NodeId parent = d->game.parent(id);
    if (isWithin(d->game, d->current, id)) {
        d->current = parent; // the current move is being deleted: step back out of it
    }
    d->game.remove(id);
    setModified();
    refreshNotation();
    showNode(d->current, false);
}

void MainWindow::deleteVariation(NodeId id) {
    const GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    // The variation starts at the first move (going up) that isn't its parent's main move.
    NodeId start = id;
    for (NodeId node = id; node != Game::root(); node = d->game.parent(node)) {
        if (d->game.mainChild(d->game.parent(node)) != node) {
            start = node;
            break;
        }
    }
    deleteFrom(start);
}

void MainWindow::toggleNag(NodeId id, std::uint8_t nag) {
    GameDocument* d = doc();
    if (d == nullptr || id == Game::root()) {
        return;
    }
    // One move mark ($1–$6) and one position mark ($10–$19) at a time.
    const auto group = [](std::uint8_t value) {
        if (value <= 9) {
            return 0; // move marks
        }
        return value <= 19 ? 1 : 2; // position marks, others
    };
    auto nags = d->game.node(id).nags;
    const bool had = std::find(nags.begin(), nags.end(), nag) != nags.end();
    std::erase_if(nags, [&](std::uint8_t value) { return group(value) == group(nag); });
    if (!had) {
        nags.push_back(nag);
        std::sort(nags.begin(), nags.end());
    }
    d->game.setNags(id, std::move(nags));
    setModified();
    refreshNotation();
}

void MainWindow::clearNags(NodeId id) {
    if (GameDocument* d = doc()) {
        d->game.setNags(id, {});
        setModified();
        refreshNotation();
    }
}

void MainWindow::editComment(NodeId id) {
    if (doc() == nullptr) {
        return;
    }
    showNode(id, false);
    m_commentBox->setFocus();
    m_commentBox->moveCursor(QTextCursor::End);
}

void MainWindow::onCommentEdited() {
    GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    d->game.setComment(d->current, m_commentBox->toPlainText().toStdString());
    setModified();
    m_notationRefresh.start(); // re-render once typing pauses
}

void MainWindow::onHeaderEdited(const QString& name, const QString& value) {
    GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    d->game.setHeader(name.toStdString(), value.toStdString());
    setModified();
    updateTitles();
    if (name == u"Result") {
        refreshNotation();
    }
}

} // namespace chessgui::app
