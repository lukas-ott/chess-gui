#include "MainWindow.hpp"

#include "board/BoardTheme.hpp"
#include "board/BoardView.hpp"
#include "board/EvalBar.hpp"
#include "board/PieceSet.hpp"
#include "databases/DatabaseManager.hpp"
#include "databases/DatabasesView.hpp"
#include "document/PgnIo.hpp"
#include "engine/EnginePanel.hpp"
#include "engine/EngineRegistry.hpp"
#include "engine/EngineSettings.hpp"
#include "engine/EnginesView.hpp"
#include "explorer/ExplorerPanel.hpp"
#include "kiebitz/core/annotations.hpp"
#include "kiebitz/repertoire/drill.hpp"
#include "library/Library.hpp"
#include "library/LibraryView.hpp"
#include "library/NewFileDialog.hpp"
#include "notation/NotationView.hpp"
#include "panels/GameInfoPanel.hpp"
#include "profiles/LichessLogin.hpp"
#include "profiles/ProfileStore.hpp"
#include "profiles/ProfilesView.hpp"
#include "repertoire/PracticeStats.hpp"
#include "repertoire/RepertoirePanel.hpp"
#include "repertoire/RepertoireStore.hpp"
#include "settings/Settings.hpp"
#include "settings/SettingsView.hpp"
#include "settings/Shortcuts.hpp"
#include "settings/ShortcutsPage.hpp"
#include "sound/Sounds.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"
#include "style/Layout.hpp"
#include "widgets/EmptyState.hpp"
#include "widgets/Message.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
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
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <optional>
#include <tuple>
#include <utility>

namespace kiebitz::app {
namespace {

using namespace Qt::StringLiterals;
using core::Game;
using core::NodeId;

/// "10… dxe6": a move of the game as the move list numbers it.
QString moveLabel(const Game& game, NodeId node) {
    const bool white = game.mover(node) == core::Color::White;
    return QStringLiteral("%1%2 %3")
        .arg(game.moveNumber(node))
        .arg(white ? QStringLiteral(".") : QStringLiteral("…"))
        .arg(QString::fromStdString(game.node(node).san));
}

/// The comments on a move as a reader sees them (no clocks or commands), or empty.
QString explanation(const Game& game, NodeId node) {
    QStringList parts;
    for (const std::string* comment : {&game.node(node).commentBefore, &game.node(node).comment}) {
        const core::CommentDisplay shown = core::displayComment(*comment);
        if (!shown.text.empty()) {
            parts << QString::fromStdString(shown.text);
        }
    }
    return parts.join(QLatin1Char(' '));
}

// QSettings keys (compile-time constants: nothing to construct at startup)
constexpr auto kCustomLightKey = "board/customLight"_L1;
constexpr auto kCustomDarkKey = "board/customDark"_L1;
constexpr auto kFlippedKey = "board/flipped"_L1;
constexpr auto kCustomTheme = "Custom"_L1;
constexpr auto kContinuationsLearnedKey = "hints/continuationsLearned"_L1;
constexpr auto kLibraryFolderKey = "library/folder"_L1;
constexpr int kEvalBarGap = 6; ///< between the eval bar and the board
/// Above and below the board, for the clocks; kept when there are none, so the board never moves.
constexpr int kClockRoom = 22;

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
    : QMainWindow(parent), m_style(style), m_library(new Library(this)),
      m_engineRegistry(new EngineRegistry(this)) {
    setWindowTitle(QStringLiteral("Kiebitz"));
    // Smaller than this and the tabs, the game list and the board no longer fit side by side.
    setMinimumSize(960, 620);
    setAcceptDrops(true);
    m_notationRefresh.setSingleShot(true);
    m_notationRefresh.setInterval(300);
    connect(&m_notationRefresh, &QTimer::timeout, this, &MainWindow::refreshNotation);
    m_autosave.setSingleShot(true);
    m_autosave.setInterval(1000);
    connect(&m_autosave, &QTimer::timeout, this, &MainWindow::autosave);

    // Repertoires: built from the Library's marked files. Before it edits a file, open games
    // write their pending changes into it; after, they reload from it.
    m_repertoires = new RepertoireStore(*m_library, this);
    // Practice statistics: your mistakes, per position, kept on this Mac.
    m_practiceStats = new PracticeStats({}, this);
    connect(m_practiceStats, &PracticeStats::mistakesChanged, this, [this](core::Color side) {
        if (const GameDocument* d = doc();
            d != nullptr && d->file &&
            RepertoireStore::kindOf(*d->file) == library::FileKind::Repertoire &&
            RepertoireStore::sideOf(*d->file) == side) {
            m_repertoirePanel->setMistakes(m_practiceStats->mistakes(side));
        }
    });
    m_repertoires->setBeforeEdit([this] { autosave(); });
    m_repertoires->setOpenText([this](const QString& path) -> std::shared_ptr<PgnFile> {
        for (const auto& d : m_documents) {
            if (d->file && d->file->hasText && d->file->path == path) {
                return d->file;
            }
        }
        return nullptr;
    });
    connect(m_repertoires, &RepertoireStore::fileEdited, this, &MainWindow::reloadGamesOf);
    connect(m_repertoires, &RepertoireStore::failed, this,
            [this](const QString& message) { notice(this, tr("Repertoire"), message); });
    connect(m_repertoires, &RepertoireStore::openingsReady, this, &MainWindow::updateMovesTitle);
    connect(m_repertoires, &RepertoireStore::changed, this, [this] {
        if (const GameDocument* d = doc()) {
            showNode(d->current, false); // the Repertoire tab's book status
            showFileStart();             // the whole file read: where it starts may differ
        }
    });

    buildLayout();
    restoreState(); // before the menus, which show the board's current settings
    buildMenus();
    // Every command's key, the user's own where they set one. Keys such as ← and Space need the
    // window's help on macOS; Shortcuts sees to that.
    m_shortcuts = new Shortcuts(this);
    m_shortcuts->addMenus(menuBar(), m_gameActions);
    m_shortcuts->add(m_upAction, tr("Game"), true);
    m_shortcuts->add(m_downAction, tr("Game"), true);
    m_shortcuts->load();
    m_settingsView->addSection(tr("Keyboard"), new ShortcutsPage(*m_shortcuts), tr("Folders"));
    statusBar()->setSizeGripEnabled(false);

    // The library loads in the background; the window is up before it's done.
    m_library->setCacheFile(Library::defaultCacheFile());
    m_library->setFolder(QSettings().value(kLibraryFolderKey, Library::defaultFolder()).toString());

    addDocument(std::make_unique<GameDocument>()); // an empty board to play on right away
    showSection(Section::Library);                 // but start in the Library
}

MainWindow::~MainWindow() = default;

// ---- Menus -------------------------------------------------------------------------------------

void MainWindow::buildMenus() {
    buildFileMenu();
    buildEditMenu();
    buildGameMenu();
    buildViewMenu(menuBar()->addMenu(tr("&View")));
    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAction = helpMenu->addAction(tr("&About Kiebitz"), this, &MainWindow::showAbout);
    aboutAction->setMenuRole(QAction::AboutRole);
}

void MainWindow::buildFileMenu() {
    auto* menu = menuBar()->addMenu(tr("&File"));
    menu->addAction(tr("New &Tab"), QKeySequence::AddTab, this,
                    [this] { addDocument(std::make_unique<GameDocument>()); });
    menu->addAction(tr("&New File…"), QKeySequence::New, this, [this] { newFile(); });
    menu->addAction(tr("&Open…"), QKeySequence::Open, this, [this] {
        const QStringList paths = QFileDialog::getOpenFileNames(
            this, tr("Open PGN"), m_library->folder(), tr("PGN files (*.pgn);;All files (*)"));
        for (const auto& path : paths) {
            openFile(path);
        }
    });
    menu->addAction(tr("&Library"), QKeySequence(Qt::CTRL | Qt::Key_L), this,
                    [this] { showSection(Section::Library); });
    menu->addAction(tr("&Board"), QKeySequence(Qt::CTRL | Qt::Key_1), this,
                    [this] { showSection(Section::Board); });
    menu->addAction(tr("&Databases"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D), this,
                    [this] { showSection(Section::Databases); });
    menu->addAction(tr("&Profiles"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this,
                    [this] { showSection(Section::Profiles); });
    menu->addAction(tr("&Engines"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), this,
                    [this] { showSection(Section::Engines); });
    menu->addSeparator();
    menu->addSeparator();
    auto* saveAction = menu->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::save);
    auto* saveAsAction =
        menu->addAction(tr("Save &As…"), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    auto* closeAction = menu->addAction(tr("&Close Tab"), QKeySequence::Close, this,
                                        [this] { closeTab(m_tabs->currentIndex()); });
    m_gameActions.insert(m_gameActions.end(), {saveAction, saveAsAction, closeAction});
    menu->addSeparator();
    menu->addAction(tr("Change Library Folder…"), this, &MainWindow::chooseLibraryFolder);
    // macOS moves it to the app menu ("Kiebitz › Settings…").
    auto* settingsAction = menu->addAction(tr("Settings…"), QKeySequence::Preferences, this,
                                           [this] { showSection(Section::Settings); });
    settingsAction->setMenuRole(QAction::PreferencesRole);
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
    m_backAction->setShortcut(Qt::Key_Left);
    m_forwardAction = menu->addAction(tr("&Forward"), this, &MainWindow::goForward);
    m_forwardAction->setShortcut(Qt::Key_Right);
    m_startAction = menu->addAction(tr("Go to &Start"), this, &MainWindow::goToStart);
    m_startAction->setShortcut(Qt::Key_Home);
    m_endAction = menu->addAction(tr("Go to &End"), this, &MainWindow::goToEnd);
    m_endAction->setShortcut(Qt::Key_End);

    // ↑ ↓: choose between continuations at a branch point; elsewhere go to the start / end.
    // Not in the menu (Back and Forward say enough there), but their keys can be changed.
    m_upAction = new QAction(tr("Previous Continuation"), this);
    m_upAction->setShortcut(Qt::Key_Up);
    connect(m_upAction, &QAction::triggered, this, [this] { stepChoiceOrJump(-1); });
    m_downAction = new QAction(tr("Next Continuation"), this);
    m_downAction->setShortcut(Qt::Key_Down);
    connect(m_downAction, &QAction::triggered, this, [this] { stepChoiceOrJump(+1); });

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
    menu->addSeparator();
    // Space, as on Lichess. Typing a space in the comment box still types a space: text fields
    // claim the key before window shortcuts see it.
    auto* engineAction =
        menu->addAction(tr("Start / Stop &Engine"), this, [this] { m_enginePanel->toggle(); });
    engineAction->setShortcut(Qt::Key_Space);
    m_gameActions.insert(m_gameActions.end(),
                         {m_backAction, m_forwardAction, m_startAction, m_endAction, promoteAction,
                          mainLineAction, deleteFromAction, deleteVariationAction, commentAction,
                          engineAction});
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
        connect(action, &QAction::triggered, this, [appearance] {
            Settings::instance().setValue(Settings::kAppearance, AppStyle::toString(appearance));
        });
    }
    menu->addSeparator();

    auto* flipAction = menu->addAction(tr("&Flip Board"));
    m_flipAction = flipAction;
    flipAction->setShortcut(Qt::Key_F); // letters work in the macOS menu bar (unlike arrows)
    flipAction->setCheckable(true);
    flipAction->setChecked(m_board->isFlipped());
    connect(flipAction, &QAction::toggled, this, [this](bool on) {
        m_board->setFlipped(on);
        m_evalBar->setFlipped(on);
        refreshClocks();
        m_repertoirePanel->setViewSide(on ? core::Color::Black : core::Color::White);
        if (const GameDocument* d = doc()) {
            m_explorer->setPosition(&d->game, d->current,
                                    on ? core::Color::Black : core::Color::White);
        }
        // Remembered for other games; a game with an [Orientation] keeps its own side.
        if (const GameDocument* d = doc(); d == nullptr || d->game.header("Orientation").empty()) {
            QSettings().setValue(kFlippedKey, on);
        }
    });

    auto* coordinatesAction = menu->addAction(tr("Show &Coordinates"));
    m_coordinatesAction = coordinatesAction;
    coordinatesAction->setCheckable(true);
    coordinatesAction->setChecked(m_board->showCoordinates());
    connect(coordinatesAction, &QAction::triggered, this, [](bool on) {
        // Back on: along the edge (every square is chosen in Settings).
        Settings::instance().setValue(Settings::kCoordinates,
                                      on ? QStringLiteral("edge") : QStringLiteral("none"));
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
        connect(action, &QAction::triggered, this,
                [name = theme.name] { Settings::instance().setValue(Settings::kTheme, name); });
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
        connect(action, &QAction::triggered, this,
                [name] { Settings::instance().setValue(Settings::kPieces, name); });
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
        emit Settings::instance().changed(QString::fromLatin1(Settings::kTheme));
        return;
    }
    QSettings settings;
    settings.setValue(kCustomLightKey, light);
    settings.setValue(kCustomDarkKey, dark);
    Settings::instance().setValue(Settings::kTheme, QString(kCustomTheme));
    applySetting(QString::fromLatin1(Settings::kTheme)); // new colours, if Custom was on already
}

void MainWindow::chooseLibraryFolder() {
    const QString folder =
        QFileDialog::getExistingDirectory(this, tr("Library Folder"), m_library->folder());
    if (!folder.isEmpty()) {
        m_library->setFolder(folder);
        QSettings().setValue(kLibraryFolderKey, folder);
        showFolders();
    }
}

void MainWindow::showFolders() {
    m_settingsView->setFolders({
        {tr("Library"), m_library->folder()},
        {tr("Game databases"), m_databases->root()},
        {tr("Profiles"), m_profiles->root()},
        {tr("Downloaded engines"), engines::downloadFolder()},
    });
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
    connect(m_tabs, &QTabBar::currentChanged, this, &MainWindow::updatePage);
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

    m_libraryView = new LibraryView(*m_library, *m_repertoires, m_style);
    connect(m_libraryView, &LibraryView::openGameRequested, this, &MainWindow::openGame);
    // Practise from the Library: open the file, then start once its tab is there.
    connect(m_libraryView, &LibraryView::practiceRequested, this,
            [this](const std::shared_ptr<PgnFile>& file) {
                m_practiceOnOpen = file->path;
                openGame(file, 0);
                if (const GameDocument* d = doc();
                    d != nullptr && d->file && d->file->path == m_practiceOnOpen) {
                    m_practiceOnOpen.clear(); // it was open already
                    startPractice(false, m_repertoirePanel->practiceDepth());
                }
            });
    // The gap check: where to look, and a gap clicked.
    m_libraryView->setGapSource([this] { return m_explorer->gapSource(); });
    connect(m_libraryView, &LibraryView::gapRequested, this,
            [this](const QString& path, const std::vector<core::Move>& line, core::Move move) {
                core::Position position;
                for (const core::Move& played : line) {
                    position.play(played);
                }
                m_playOnJump = move;
                jumpToLine(path, position.hash());
            });
    connect(this, &MainWindow::fileOpened, this, [this](const QString& path) {
        if (m_jumpOnOpen && path == m_jumpOnOpen->first) {
            const std::uint64_t position = m_jumpOnOpen->second;
            m_jumpOnOpen.reset();
            goToPosition(position);
        } else if (!m_practiceOnOpen.isEmpty() && path == m_practiceOnOpen) {
            m_practiceOnOpen.clear();
            startPractice(false, m_repertoirePanel->practiceDepth());
        } else {
            goToFileStart(); // a repertoire file opens where it starts (after 1. e4 c6)
        }
    });
    connect(m_libraryView, &LibraryView::fileKindRequested, this,
            [this](const QStringList& paths, int kind, int side) {
                m_repertoires->setKind(
                    paths, static_cast<library::FileKind>(kind),
                    side < 0 ? std::nullopt
                             : std::optional(side == 0 ? core::Color::White : core::Color::Black));
            });
    connect(m_libraryView, &LibraryView::newFileRequested, this,
            [this](const QString& folder) { newFile(folder); });
    // A file or folder moved in the Library: its open tabs save to where it is now.
    connect(m_libraryView, &LibraryView::pathMoved, this,
            [this](const QString& from, const QString& to) {
                for (const auto& d : m_documents) {
                    if (!d->file) {
                        continue;
                    }
                    QString& path = d->file->path;
                    if (path == from) {
                        path = to;
                    } else if (path.startsWith(from + QLatin1Char('/'))) {
                        path = to + path.mid(from.size());
                    }
                }
                updateTitles();
            });
    connect(m_libraryView, &LibraryView::pathRemoved, this, [this](const QString& path) {
        for (int tab = m_tabs->count() - 1; tab >= 0; --tab) {
            GameDocument* d = docAt(tab);
            if (d != nullptr && d->file &&
                (d->file->path == path || d->file->path.startsWith(path + QLatin1Char('/')))) {
                d->modified = false; // it's going: nothing to save
                closeTab(tab);
            }
        }
    });
    connect(m_libraryView, &LibraryView::changeFolderRequested, this,
            &MainWindow::chooseLibraryFolder);

    // Game databases: big collections to search, opened on the board one game at a time.
    m_databases = new DatabaseManager({}, this);
    m_databasesView = new DatabasesView(*m_databases, m_style);
    connect(m_databasesView, &DatabasesView::openGameRequested, this, &MainWindow::openGameCopy);
    // Profiles: you and the people whose games you follow.
    m_sounds = new Sounds(this);
    m_lichess = new LichessLogin(LichessLogin::privateFile(), this);
    connect(m_lichess, &LichessLogin::failed, this,
            [this](const QString& message) { notice(this, tr("Lichess"), message); });
    // Signed in in the browser: back to Kiebitz.
    connect(m_lichess, &LichessLogin::signedInNow, this, [this] {
        raise();
        activateWindow();
    });
    m_profiles = new ProfileStore({}, this);
    m_profiles->setLichessToken([this](const QString& username) {
        const QByteArray own = m_lichess->token(username);
        return own.isEmpty() ? m_lichess->token() : own; // any login makes it faster
    });
    m_profilesView = new ProfilesView(*m_profiles, *m_lichess, *m_repertoires, m_style);
    connect(m_profilesView, &ProfilesView::openGameRequested, this, &MainWindow::openGameCopy);
    // The theory check: a game at the move that left the book, or the repertoire there.
    connect(m_profilesView, &ProfilesView::openGameAtRequested, this,
            [this](const core::Game& game, int ply) {
                openGameCopy(game);
                const std::vector<NodeId> line = game.mainLine();
                if (ply >= 0 && std::cmp_less(ply, line.size())) {
                    showNode(line[static_cast<std::size_t>(ply)], false);
                }
            });
    connect(m_profilesView, &ProfilesView::repertoireRequested, this,
            [this](core::Color side, const std::vector<core::Move>& line) {
                core::Position position;
                for (const core::Move& move : line) {
                    position.play(move);
                }
                const QStringList files = m_repertoires->filesWith(side, position.hash());
                if (!files.isEmpty()) {
                    jumpToLine(files.front(), position.hash());
                }
            });

    m_enginesView = new EnginesView(*m_engineRegistry, m_style);
    m_enginesView->setObjectName(QStringLiteral("enginesView"));

    // The board page: the game tabs over the board and its panels.
    auto* boardPage = new QWidget;
    auto* boardLayout = new QVBoxLayout(boardPage);
    boardLayout->setContentsMargins(0, 0, 0, 0);
    boardLayout->setSpacing(0);
    boardLayout->addWidget(tabRow);
    boardLayout->addWidget(buildAnalysisPage(), 1);

    m_pages = new QStackedWidget; // in Section order
    m_pages->addWidget(boardPage);
    m_pages->addWidget(m_libraryView);
    m_pages->addWidget(m_databasesView);
    m_pages->addWidget(m_profilesView);
    m_pages->addWidget(m_enginesView);
    m_settingsView = new SettingsView(m_style);
    connect(m_settingsView, &SettingsView::customColorsRequested, this,
            &MainWindow::chooseCustomColors);
    connect(m_settingsView, &SettingsView::changeLibraryFolderRequested, this,
            &MainWindow::chooseLibraryFolder);
    connect(m_settingsView, &SettingsView::showEnginesRequested, this,
            [this] { showSection(Section::Engines); });
    connect(m_settingsView, &SettingsView::soundTestRequested, this,
            [this] { m_sounds->play(Sounds::Kind::Move); });
    connect(&Settings::instance(), &Settings::changed, this, [this](const QString& key) {
        if (key == QLatin1String(Settings::kSoundSet)) {
            m_sounds->play(Sounds::Kind::Move); // a new set: heard at once
        }
    });
    m_pages->addWidget(m_settingsView);
    showFolders();
    connect(&Settings::instance(), &Settings::changed, this, &MainWindow::applySetting);

    // The sidebar: one icon per section, as in En Croissant.
    auto* sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(52);
    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(0, 8, 0, 8);
    sideLayout->setSpacing(4);
    m_sidebar = new QButtonGroup(this);
    for (const auto& [section, name, tip] :
         {std::tuple{Section::Board, "sideBoard", tr("Board (⌘1)")},
          std::tuple{Section::Library, "sideLibrary", tr("Library (⌘L)")},
          std::tuple{Section::Databases, "sideDatabases", tr("Databases (⇧⌘D)")},
          std::tuple{Section::Profiles, "sideProfiles", tr("Profiles (⇧⌘P)")},
          std::tuple{Section::Engines, "sideEngines", tr("Engines (⇧⌘E)")}}) {
        auto* button = new QToolButton;
        button->setObjectName(QString::fromLatin1(name));
        button->setProperty("role", "sideNav");
        button->setToolTip(tip);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setIconSize(QSize(22, 22));
        button->setFixedSize(52, 44);
        m_sidebar->addButton(button, static_cast<int>(section));
        sideLayout->addWidget(button);
    }
    sideLayout->addStretch();
    // Settings: at the bottom, apart from the sections.
    auto* settingsButton = new QToolButton;
    settingsButton->setObjectName(QStringLiteral("sideSettings"));
    settingsButton->setProperty("role", "sideNav");
    settingsButton->setToolTip(tr("Settings (⌘,)"));
    settingsButton->setCheckable(true);
    settingsButton->setFocusPolicy(Qt::NoFocus);
    settingsButton->setIconSize(QSize(22, 22));
    settingsButton->setFixedSize(52, 44);
    m_sidebar->addButton(settingsButton, static_cast<int>(Section::Settings));
    sideLayout->addWidget(settingsButton);
    m_sidebar->setExclusive(true);
    connect(m_sidebar, &QButtonGroup::idClicked, this,
            [this](int id) { showSection(static_cast<Section>(id)); });
    connect(m_sidebar, &QButtonGroup::buttonToggled, this, &MainWindow::updateSidebarIcons);

    auto* central = new QWidget;
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(sidebar);
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
    // The moves, or (while practising a repertoire) the practice card in their place.
    m_movesStack = new QStackedWidget;
    m_movesStack->addWidget(buildMovesCard());
    m_movesStack->addWidget(buildPracticeCard());
    m_sideSplitter->addWidget(m_movesStack);
    m_sideSplitter->addWidget(buildPanelsCard());
    m_sideSplitter->setChildrenCollapsible(false);
    m_sideSplitter->setStretchFactor(0, 2);
    m_sideSplitter->setStretchFactor(1, 1);

    // The board's column keeps room on its right for the eval bar, which placeEvalBar() puts
    // exactly halfway between the board's squares and the moves panel. The room stays when the
    // bar is hidden, so the board never shifts when engines start or stop.
    auto* boardColumn = new QWidget;
    auto* boardLayout = new QHBoxLayout(boardColumn);
    boardLayout->setContentsMargins(0, kClockRoom, EvalBar::kWidth + kEvalBarGap, kClockRoom);
    boardLayout->addWidget(m_board, 1);
    m_board->installEventFilter(this);

    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->setObjectName(QStringLiteral("mainSplitter"));
    m_mainSplitter->addWidget(boardColumn);
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
    m_evalBar = new EvalBar(page); // floats over the page; see placeEvalBar()
    m_evalBar->hide();
    for (QLabel*& clock : m_clocks) {
        clock = new QLabel(page);
        clock->setProperty("role", "boardClock");
        clock->setAlignment(Qt::AlignCenter);
        clock->hide();
    }
    m_clocks[0]->setObjectName(QStringLiteral("clockTop"));
    m_clocks[1]->setObjectName(QStringLiteral("clockBottom"));
    connect(m_mainSplitter, &QSplitter::splitterMoved, this, &MainWindow::placeEvalBar);
    return page;
}

QWidget* MainWindow::buildMovesCard() {
    auto* frame = card();
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(layout::kCard);
    layout->setSpacing(layout::kCardSpacing);
    m_movesTitle = label(tr("MOVES"), "cardTitle");
    m_movesTitle->setObjectName(QStringLiteral("movesTitle"));
    // The title, and how to show the moves: flowing text or rows (remembered).
    auto* titleRow = new QHBoxLayout;
    titleRow->addWidget(m_movesTitle, 1);
    auto* views = new QWidget;
    views->setObjectName(QStringLiteral("notationViews"));
    views->setProperty("role", "segmented");
    auto* viewsLayout = new QHBoxLayout(views);
    viewsLayout->setContentsMargins(2, 2, 2, 2);
    viewsLayout->setSpacing(2);
    auto* viewGroup = new QButtonGroup(views);
    m_notationViews = viewGroup;
    const bool rows = Settings::instance().flag(Settings::kNotationRows);
    for (const auto& [id, text, tip] :
         {std::tuple{0, tr("Text"), tr("Moves as flowing text")},
          std::tuple{1, tr("Rows"), tr("Moves in rows, with comments and variations between")}}) {
        auto* segment = new QPushButton(text);
        segment->setObjectName(id == 0 ? QStringLiteral("notationText")
                                       : QStringLiteral("notationRows"));
        segment->setProperty("role", "segment");
        segment->setCheckable(true);
        segment->setChecked((id == 1) == rows);
        segment->setFocusPolicy(Qt::NoFocus);
        segment->setToolTip(tip);
        viewGroup->addButton(segment, id);
        viewsLayout->addWidget(segment);
    }
    titleRow->addWidget(views);
    layout->addLayout(titleRow);

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
    connect(m_notation, &NotationView::startRequested, this, &MainWindow::setFileStart);
    m_notation->setMinimumHeight(200); // always room for a good few lines of moves
    m_notation->setRows(rows);
    connect(viewGroup, &QButtonGroup::idClicked, this,
            [](int id) { Settings::instance().setValue(Settings::kNotationRows, id == 1); });
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
    m_enginePanel = new EnginePanel(*m_engineRegistry, m_style);
    connect(m_enginePanel, &EnginePanel::manageEnginesRequested, this,
            [this] { showSection(Section::Engines); });
    m_enginePanel->setObjectName(QStringLiteral("enginePanel"));
    connect(m_enginePanel, &EnginePanel::playRequested, this, &MainWindow::playMoves);
    connect(m_enginePanel, &EnginePanel::analysisChanged, this,
            [this](const std::vector<core::Move>& best, std::optional<engine::Score> score,
                   bool running) {
                m_board->setEngineMoves(best);
                m_evalBar->setScore(score);
                if (running && !m_evalBar->isVisible()) {
                    placeEvalBar();
                    m_evalBar->raise();
                }
                m_evalBar->setVisible(running); // its space is kept: nothing else moves
            });
    panels->addTab(m_enginePanel, tr("Engine"));
    m_explorer = new ExplorerPanel(*m_databases, *m_profiles, *m_repertoires, *m_lichess, m_style);
    connect(m_explorer, &ExplorerPanel::movePlayed, this, [this](core::Move move) {
        if (!m_drill) { // in practice the explorer would give the answers away
            playMoves({move});
        }
    });
    connect(m_explorer, &ExplorerPanel::openGameRequested, this, &MainWindow::openGameCopy);
    connect(m_explorer, &ExplorerPanel::showDatabasesRequested, this,
            [this] { showSection(Section::Databases); });
    panels->addTab(m_explorer, tr("Explorer"));
    m_repertoirePanel = new RepertoirePanel(*m_repertoires, m_style);
    connect(m_repertoirePanel, &RepertoirePanel::addLineRequested, this, [this](core::Color side) {
        if (const GameDocument* d = doc();
            d != nullptr && addLineAsking(side, lineTo(d->game, d->current))) {
            statusBar()->showMessage(side == core::Color::White
                                         ? tr("Added to your White repertoire")
                                         : tr("Added to your Black repertoire"),
                                     kStatusMs);
        }
    });
    connect(m_repertoirePanel, &RepertoirePanel::jumpRequested, this, &MainWindow::jumpToLine);
    // A repertoire move clicked: play the line to it from the start (existing moves are reused).
    connect(m_repertoirePanel, &RepertoirePanel::playLineRequested, this,
            [this](const std::vector<core::Move>& line) {
                if (GameDocument* d = doc(); d != nullptr) {
                    d->current = core::Game::root();
                    playMoves(line);
                }
            });
    connect(m_repertoirePanel, &RepertoirePanel::practiceRequested, this,
            [this](bool fromHere, int depth) { startPractice(fromHere, depth); });
    connect(m_repertoirePanel, &RepertoirePanel::wholeRepertoireRequested, this,
            &MainWindow::practiceWholeRepertoire);
    connect(m_repertoirePanel, &RepertoirePanel::mistakesRequested, this,
            [this] { startPractice(false, 0, true); });
    panels->addTab(m_repertoirePanel, tr("Repertoire"));
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

    m_board->setFlipped(settings.value(kFlippedKey, false).toBool());
    m_evalBar->setFlipped(m_board->isFlipped());
    for (const char* key :
         {Settings::kTheme, Settings::kPieces, Settings::kCoordinates, Settings::kFigurines}) {
        applySetting(QString::fromLatin1(key));
    }
}

void MainWindow::applySetting(const QString& key) {
    const Settings& settings = Settings::instance();
    const auto is = [&key](const char* name) {
        return key == QLatin1String(name);
    };
    if (is(Settings::kTheme)) {
        const QString theme = settings.string(Settings::kTheme);
        const QSettings stored;
        m_board->setTheme(theme == kCustomTheme
                              ? BoardTheme::custom(stored.value(kCustomLightKey).value<QColor>(),
                                                   stored.value(kCustomDarkKey).value<QColor>())
                              : BoardTheme::preset(theme));
        m_libraryView->setBoardLook(m_board->theme(), m_board->pieceSetName());
        // The menus' ticks (at the start the menus aren't there yet: they read the board).
        for (auto* action : m_themeGroup != nullptr ? m_themeGroup->actions() : QList<QAction*>{}) {
            action->setChecked(theme == kCustomTheme ? action == m_customColorsAction
                                                     : action->text() == m_board->theme().name);
        }
    } else if (is(Settings::kPieces)) {
        m_board->setPieceSet(settings.string(Settings::kPieces));
        m_libraryView->setBoardLook(m_board->theme(), m_board->pieceSetName());
        for (auto* action :
             m_piecesGroup != nullptr ? m_piecesGroup->actions() : QList<QAction*>{}) {
            action->setChecked(action->text() == m_board->pieceSetName());
        }
    } else if (is(Settings::kCoordinates) || is(Settings::kAnimation) || is(Settings::kLegalDots) ||
               is(Settings::kLastMove) || is(Settings::kMoveMethod) || is(Settings::kAutoQueen)) {
        BoardView::Options options;
        const QString coordinates = settings.string(Settings::kCoordinates);
        options.coordinates = BoardView::Options::Coordinates::Edge;
        if (coordinates == u"none") {
            options.coordinates = BoardView::Options::Coordinates::None;
        } else if (coordinates == u"all") {
            options.coordinates = BoardView::Options::Coordinates::All;
        }
        options.slideMs = settings.animationMs();
        options.legalDots = settings.flag(Settings::kLegalDots);
        options.lastMove = settings.flag(Settings::kLastMove);
        const QString method = settings.string(Settings::kMoveMethod);
        options.moveMethod = BoardView::Options::MoveMethod::Both;
        if (method == u"drag") {
            options.moveMethod = BoardView::Options::MoveMethod::Drag;
        } else if (method == u"click") {
            options.moveMethod = BoardView::Options::MoveMethod::Click;
        }
        options.autoQueen = settings.flag(Settings::kAutoQueen);
        m_board->setOptions(options);
        if (m_coordinatesAction != nullptr) {
            const QSignalBlocker block(m_coordinatesAction);
            m_coordinatesAction->setChecked(m_board->showCoordinates());
        }
    } else if (is(Settings::kNotationRows)) {
        const bool rows = settings.flag(Settings::kNotationRows);
        m_notation->setRows(rows);
        if (auto* button = m_notationViews->button(rows ? 1 : 0)) {
            button->setChecked(true);
        }
    } else if (is(Settings::kFigurines)) {
        m_notation->setFigurines(settings.flag(Settings::kFigurines));
    } else if (is(Settings::kAppearance)) {
        const auto appearance = AppStyle::fromString(settings.string(Settings::kAppearance));
        m_style.setAppearance(appearance);
        const QStringList names{tr("Same as System"), tr("Light"), tr("Dark")};
        for (auto* action :
             m_appearanceGroup != nullptr ? m_appearanceGroup->actions() : QList<QAction*>{}) {
            action->setChecked(action->text() == names.value(static_cast<int>(appearance)));
        }
    } else if (is(Settings::kAccent)) {
        const QString accent = settings.string(Settings::kAccent);
        m_style.setAccent(accent.isEmpty() ? QColor() : QColor(accent));
    } else if (is(Settings::kTextSize)) {
        m_style.setTextScale(settings.number(Settings::kTextSize));
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    fitBoardColumn();
    // Once more when the layout has settled: right now the splitters still have their old size.
    QMetaObject::invokeMethod(this, &MainWindow::fitBoardColumn, Qt::QueuedConnection);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_board && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        placeEvalBar();
    }
    // The wheel (or two fingers on a trackpad) over the board: back and forward through the
    // moves, one per notch (a trackpad's small steps are added up first).
    if (watched == m_board && event->type() == QEvent::Wheel &&
        Settings::instance().flag(Settings::kWheel)) {
        const auto* wheel = dynamic_cast<const QWheelEvent*>(event);
        m_wheel += wheel->angleDelta().y();
        while (m_wheel >= 120) {
            m_wheel -= 120;
            goBack();
        }
        while (m_wheel <= -120) {
            m_wheel += 120;
            goForward();
        }
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::placeEvalBar() {
    // As tall as the board's squares, and centred in the gap between them and the moves panel.
    const QWidget* page = m_evalBar->parentWidget();
    const QRect a8 = m_board->squareRect(core::Square::at(0, 7));
    const QRect h1 = m_board->squareRect(core::Square::at(7, 0));
    const QRect squares =
        QRect(m_board->mapTo(page, a8.topLeft()), m_board->mapTo(page, h1.bottomRight()))
            .normalized()
            .united(
                QRect(m_board->mapTo(page, h1.topLeft()), m_board->mapTo(page, a8.bottomRight()))
                    .normalized());
    const int panel = m_sideSplitter->mapTo(page, QPoint(0, 0)).x();
    const int x = squares.right() + 1 + ((panel - squares.right() - 1 - EvalBar::kWidth) / 2);
    m_evalBar->setGeometry(x, squares.top(), EvalBar::kWidth, squares.height());
    // The clocks: right-aligned with the squares, just above and just below them.
    for (std::size_t k = 0; k < m_clocks.size(); ++k) {
        QLabel* clock = m_clocks[k];
        const QSize size(std::max(clock->sizeHint().width(), 64), 24);
        const int y = k == 0 ? squares.top() - size.height() - 6 : squares.bottom() + 7;
        clock->setGeometry(squares.right() + 1 - size.width(), y, size.width(), size.height());
    }
}

void MainWindow::refreshClocks() {
    const GameDocument* d = doc();
    std::array<QString, 2> times; // White, Black: the last clock of each up to here
    bool any = false;
    if (d != nullptr && !m_drill) {
        const Game& game = d->game;
        const bool whiteFirst = game.startPosition().sideToMove() == core::Color::White;
        std::vector<NodeId> line = game.path(d->current);
        const std::size_t here = line.size();
        for (NodeId node = game.mainChild(d->current); node != core::kNoNode;
             node = game.mainChild(node)) {
            line.push_back(node); // on along the main line: does the game have clocks at all?
        }
        for (std::size_t k = 1; k < line.size(); ++k) { // line[0] is the start
            const std::string clock = core::displayComment(game.node(line[k]).comment).clock;
            if (clock.empty()) {
                continue;
            }
            any = true;
            if (k < here) {
                const bool white = ((k % 2) == 1) == whiteFirst; // who made move k
                times[white ? 0 : 1] = QString::fromStdString(clock);
            }
        }
    }
    // White at the bottom unless the board is flipped.
    const bool flipped = m_board->isFlipped();
    const std::array<QString, 2> shown{flipped ? times[0] : times[1],
                                       flipped ? times[1] : times[0]};
    for (std::size_t k = 0; k < 2; ++k) {
        m_clocks[k]->setText(shown[k]);
        m_clocks[k]->setVisible(any && !shown[k].isEmpty());
    }
    placeEvalBar();
}

void MainWindow::fitBoardColumn() {
    // The board is square, so a board column wider than it is tall only adds empty space.
    // Give it exactly its height and the rest to the side column.
    const QList<int> sizes = m_mainSplitter->sizes();
    if (sizes.size() != 2 || m_mainSplitter->height() <= 0) {
        return;
    }
    const int total = sizes[0] + sizes[1];
    const int bar = EvalBar::kWidth + kEvalBarGap; // always reserved, shown or not
    const int side = std::max(m_sideSplitter->minimumWidth(),
                              total - (m_mainSplitter->height() - (2 * kClockRoom)) - bar);
    m_mainSplitter->setSizes({total - side, side});
    if (m_defaultSideSplit) {
        // The moves are what you read: they get two thirds of the height, the panels the rest.
        const int height = m_mainSplitter->height();
        const int moves = (height * 2) / 3;
        m_sideSplitter->setSizes({moves, height - moves});
    }
    placeEvalBar();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // No questions when quitting either: games from files are saved (as autosave would), new
    // games that aren't in a file just go.
    for (const auto& document : m_documents) {
        if (inFile(*document) && !saveNow(*document)) {
            event->ignore(); // couldn't be saved: stay open (saveNow said why)
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
    notice(this, tr("About Kiebitz"),
           tr("Kiebitz %1: a fast native chess app for opening preparation.")
               .arg(QApplication::applicationVersion()),
           tr("Licensed under the GNU GPL v3. Piece sets: cburnett and merida (GPLv2+), "
              "chessnut (Apache 2.0), fantasy (MIT), via Lichess."));
}

// ---- Documents and tabs ------------------------------------------------------------------------

GameDocument* MainWindow::docAt(int tab) const {
    if (tab < 0) {
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
    // Only the board page has a current game (menus act on it); the other pages have none.
    return m_section == Section::Board ? docAt(m_tabs->currentIndex()) : nullptr;
}

void MainWindow::addDocument(std::unique_ptr<GameDocument> document) {
    // An untouched empty game is replaced rather than left lying around in its own tab.
    for (int tab = 0; tab < m_tabs->count(); ++tab) {
        if (GameDocument* existing = docAt(tab); existing->isPristine()) {
            const int id = existing->id; // the tab knows the document by this id: keep it
            *existing = std::move(*document);
            existing->id = id;
            const QSignalBlocker block(m_tabs); // updatePage() below shows it either way
            m_tabs->setCurrentIndex(tab);
            showSection(Section::Board);
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
    close->setFixedSize(26, 26); // 20 x 20 for the ✕, plus its top and right margins
    close->setAutoRaise(true);
    close->setFocusPolicy(Qt::NoFocus);
    close->setToolTip(tr("Close tab (⌘W)"));
    const auto& c = m_style.colors();
    close->setIcon(makeIcon(IconKind::Close, c.muted, c.dimmed, 12));
    connect(close, &QToolButton::clicked, this, [this, close] {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (m_tabs->tabButton(i, QTabBar::RightSide) == close) {
                closeTab(i);
                return;
            }
        }
    });
    m_tabs->setTabButton(tab, QTabBar::LeftSide, nullptr);
    m_tabs->setTabButton(tab, QTabBar::RightSide, close);
    {
        const QSignalBlocker block(m_tabs);
        m_tabs->setCurrentIndex(tab);
    }
    showSection(Section::Board);
}

void MainWindow::showSection(Section section) {
    m_section = section;
    if (auto* button = m_sidebar->button(static_cast<int>(section))) {
        button->setChecked(true);
    }
    updatePage();
}

void MainWindow::updatePage() {
    m_notationRefresh.stop();
    const GameDocument* d = doc();
    if (m_drill && (d == nullptr || d->id != m_drillDocument)) {
        stopPractice(); // another tab or page: practice ends
    }
    for (auto* action : m_gameActions) {
        action->setEnabled(d != nullptr);
    }
    // The arrow keys move through the game only on the board; in the Library they belong to the
    // file tree and the game list.
    m_shortcuts->setGameKeysEnabled(d != nullptr);
    m_pages->setCurrentIndex(static_cast<int>(m_section));
    if (d == nullptr) {
        m_enginePanel->setPosition(std::nullopt); // nothing to analyse off the board
        m_notation->setGame(nullptr, Game::root());
        m_info->setGame(nullptr);
        m_explorer->setPosition(nullptr, Game::root(), core::Color::White);
    } else {
        showDocument();
        m_board->setFocus();
        fitBoardColumn();
    }
    updateTitles();
}

void MainWindow::updateSidebarIcons() {
    const auto& c = m_style.colors();
    for (auto* button : m_sidebar->buttons()) {
        const auto section = static_cast<Section>(m_sidebar->id(button));
        IconKind kind = IconKind::Board;
        if (section == Section::Library) {
            kind = IconKind::Library;
        } else if (section == Section::Databases) {
            kind = IconKind::Database;
        } else if (section == Section::Profiles) {
            kind = IconKind::Person;
        } else if (section == Section::Engines) {
            kind = IconKind::Engine;
        } else if (section == Section::Settings) {
            kind = IconKind::Gear;
        }
        button->setIcon(makeIcon(kind, button->isChecked() ? c.text : c.muted, c.dimmed, 22));
    }
}

void MainWindow::closeTab(int tab) {
    GameDocument* d = docAt(tab);
    if (d == nullptr) {
        return;
    }
    // No question when a tab closes: a game from a file is saved (as autosave would), any other
    // game (a new one, one opened from a database) just goes. Quitting does the same.
    if (inFile(*d) && !saveNow(*d)) {
        return; // couldn't be saved: keep it open (saveNow said why)
    }
    m_tabs->removeTab(tab); // switches tabs first, so nothing shows the game being removed
    std::erase_if(m_documents, [d](const auto& document) { return document.get() == d; });
    if (m_tabs->count() == 0) {
        addDocument(std::make_unique<GameDocument>()); // the board always has a game
    }
    updateTitles();
}

bool MainWindow::inFile(const GameDocument& document) {
    return document.file && !document.file->path.isEmpty() && document.gameIndex >= 0 &&
           document.file->hasText;
}

void MainWindow::autosave() {
    m_autosave.stop();
    std::vector<std::shared_ptr<PgnFile>> files;
    for (const auto& d : m_documents) {
        if (!d->modified || !inFile(*d)) {
            continue;
        }
        if (m_writing.contains(d->file->path)) {
            m_autosave.start(); // its file is still being written: try again in a moment
            continue;
        }
        std::optional<std::string> text =
            pgnio::replaceGame(*d->file, d->gameIndex, pgn::writeGame(d->game));
        if (!text) {
            continue;
        }
        // Into the shared text now, so another tab of the same file builds on this change.
        d->file->text = std::move(*text);
        d->file->games = pgn::indexGames(d->file->text);
        d->modified = false;
        if (std::find(files.begin(), files.end(), d->file) == files.end()) {
            files.push_back(d->file);
        }
    }
    for (const auto& file : files) {
        const QString path = file->path;
        m_writing << path;
        whenReady(this, pgnio::write(path, file->text), [this, path](const QString& error) {
            m_writing.removeOne(path);
            if (!error.isEmpty()) {
                notice(this, tr("Couldn't save"), error);
                emit fileError(error);
                return;
            }
            emit fileSaved(path);
        });
    }
    updateTitles();
}

bool MainWindow::saveNow(GameDocument& document) {
    if (!document.modified) {
        return true;
    }
    if (inFile(document)) { // saved anyway a moment later: do it now, and wait for it
        // An autosave of this file still being written must not land after this one.
        QElapsedTimer waited;
        waited.start();
        while (m_writing.contains(document.file->path) && waited.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
        }
        std::optional<std::string> text =
            pgnio::replaceGame(*document.file, document.gameIndex, pgn::writeGame(document.game));
        if (text) {
            const QString error = pgnio::write(document.file->path, *text).result();
            if (error.isEmpty()) {
                document.file->text = std::move(*text);
                document.file->games = pgn::indexGames(document.file->text);
                document.modified = false;
                return true;
            }
            notice(this, tr("Couldn't save"), error);
        }
        return false;
    }
    return true; // not in a file: nothing to save it to
}

void MainWindow::save() {
    GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    if (!inFile(*d)) {
        saveAs();
        return;
    }
    d->modified = true; // ⌘S always writes, even right after an autosave
    autosave();
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
                notice(this, tr("Couldn't save"), error);
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
    GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    if (!d->modified) {
        d->modified = true;
        updateTitles();
    }
    if (inFile(*d)) {
        m_autosave.start(); // (again): saves a second after the last change
    }
}

void MainWindow::updateTitles() {
    for (int tab = 0; tab < m_tabs->count(); ++tab) {
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
        QString page = tr("Library");
        if (m_section == Section::Engines) {
            page = tr("Engines");
        } else if (m_section == Section::Databases) {
            page = tr("Databases");
        } else if (m_section == Section::Profiles) {
            page = tr("Profiles");
        }
        setWindowTitle(page + tr(" — Kiebitz"));
        setWindowModified(false);
        return;
    }
    const QString title = d->modified ? d->title().mid(2) : d->title();
    setWindowTitle(title + QStringLiteral("[*] — Kiebitz"));
    setWindowModified(d->modified);
}

// ---- Opening -----------------------------------------------------------------------------------

void MainWindow::openFile(const QString& path) {
    statusBar()->showMessage(tr("Opening %1…").arg(QFileInfo(path).fileName()));
    whenReady(this, pgnio::load(path), [this, path](const pgnio::Loaded& loaded) {
        statusBar()->clearMessage();
        if (!loaded.file) {
            notice(this, tr("Couldn't open"), loaded.error);
            emit fileError(loaded.error);
            return;
        }
        if (loaded.file->games.empty()) {
            const QString message = tr("%1 contains no games.").arg(QFileInfo(path).fileName());
            notice(this, tr("No games"), message);
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
            notice(this, tr("Couldn't read the game"), loaded.error);
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
    for (int tab = 0; tab < m_tabs->count(); ++tab) {
        const GameDocument* d = docAt(tab);
        if (d != nullptr && d->file && !file->path.isEmpty() && d->file->path == file->path &&
            d->gameIndex == index) {
            m_tabs->setCurrentIndex(tab);
            showSection(Section::Board);
            return;
        }
    }
    if (!file->hasText) {
        // From the Library, which keeps only the index: a game's tab saves into the file's text,
        // so it needs all of it. The game is found again by where it starts in the file.
        const std::size_t offset =
            index >= 0 && static_cast<std::size_t>(index) < file->games.size()
                ? file->games[static_cast<std::size_t>(index)].offset
                : 0;
        const auto indexIn = [index, offset](const PgnFile& full) {
            const auto& games = full.games;
            const auto match = std::find_if(games.begin(), games.end(),
                                            [offset](const auto& g) { return g.offset == offset; });
            return match != games.end() ? static_cast<int>(match - games.begin())
                                        : std::min(index, static_cast<int>(games.size()) - 1);
        };
        // Another tab already has this file: share its text, so the tabs' saves add up. Checked
        // again once the file is read, in case another tab opened it meanwhile.
        const auto openInTab = [this, indexIn, path = file->path] {
            for (const auto& d : m_documents) {
                if (d->file && d->file->hasText && d->file->path == path) {
                    openGame(d->file, indexIn(*d->file));
                    return true;
                }
            }
            return false;
        };
        if (openInTab()) {
            return;
        }
        whenReady(this, pgnio::load(file->path),
                  [this, indexIn, openInTab](const pgnio::Loaded& loaded) {
                      if (openInTab()) {
                          return;
                      }
                      if (!loaded.file) {
                          notice(this, tr("Couldn't read the game"), loaded.error);
                          emit fileError(loaded.error);
                          return;
                      }
                      openGame(loaded.file, indexIn(*loaded.file));
                  });
        return;
    }
    whenReady(this, pgnio::parse(file, index), [this, file, index](const pgnio::Parsed& parsed) {
        if (!parsed.game) {
            notice(this, tr("Couldn't read the game"), parsed.error);
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
        notice(this, tr("Invalid FEN"), QString::fromStdString(position.error().message));
        return;
    }
    auto document = std::make_unique<GameDocument>();
    document->game = Game(std::move(*position));
    addDocument(std::move(document));
}

// ---- Showing the game --------------------------------------------------------------------------

void MainWindow::orientBoard(const GameDocument& document) {
    const QString side = toQString(document.game.header("Orientation"));
    bool flipped = QSettings().value(kFlippedKey, false).toBool();
    if (side.compare(u"black", Qt::CaseInsensitive) == 0) {
        flipped = true;
    } else if (side.compare(u"white", Qt::CaseInsensitive) == 0) {
        flipped = false;
    }
    if (m_board->isFlipped() != flipped) {
        m_board->setFlipped(flipped);
        m_evalBar->setFlipped(flipped);
    }
    if (m_flipAction != nullptr) {
        const QSignalBlocker block(m_flipAction);
        m_flipAction->setChecked(flipped);
    }
}

void MainWindow::showDocument() {
    const GameDocument* d = doc();
    orientBoard(*d);
    m_notation->setGame(&d->game, d->current);
    showFileStart();
    m_info->setGame(&d->game,
                    d->file && RepertoireStore::kindOf(*d->file) == library::FileKind::Repertoire);
    showNode(d->current, false);
}

void MainWindow::refreshNotation() {
    m_notationRefresh.stop();
    if (const GameDocument* d = doc()) {
        m_notation->setGame(&d->game, d->current);
        showFileStart(); // an edit may move where the file's lines part
    }
}

void MainWindow::showNode(NodeId id, bool animate) {
    GameDocument* d = doc();
    if (d == nullptr || m_drill) { // while practising, the practice drives the board
        return;
    }
    d->current = id;
    const core::Node& node = d->game.node(id);
    const auto lastMove = id == Game::root() ? std::nullopt : std::optional(node.move);
    m_board->setPosition(d->game.positionAt(id), lastMove, animate);
    if (animate) {
        playMoveSound(d->game, id); // a move played, or one step forward
    }
    m_board->setDrawings(node.arrows, node.squareMarks);
    refreshClocks();
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
    m_enginePanel->setPosition(m_board->position());
    const std::optional<core::Color> practiceSide = repertoireSideOf(*d);
    const bool repertoireFile = practiceSide.has_value();
    const core::Color viewSide = m_board->isFlipped() ? core::Color::Black : core::Color::White;
    m_repertoirePanel->setViewSide(viewSide);
    m_explorer->setPosition(&d->game, id, viewSide);
    if (repertoireFile) { // your mistakes in it (read once, then kept up to date)
        const core::Color side = *practiceSide;
        bool& asked = m_mistakesAsked[side == core::Color::White ? 0 : 1];
        if (m_practiceStats->mistakes(side) == nullptr && !asked) {
            asked = true;
            m_practiceStats->load(side);
        }
    }
    const NodeId start = fileStartNode(*d);
    m_repertoirePanel->setGame(
        &d->game, id, repertoireFile, practiceSide.value_or(core::Color::White),
        repertoireFile ? m_practiceStats->mistakes(*practiceSide) : nullptr,
        start == core::kNoNode ? Game::root() : start, d->wholeRepertoire.has_value());
    updateMovesTitle();
}

std::vector<core::Move> MainWindow::lineTo(const Game& game, NodeId id) {
    std::vector<core::Move> line;
    for (const NodeId node : game.path(id)) {
        if (node != Game::root()) {
            line.push_back(game.node(node).move);
        }
    }
    return line;
}

void MainWindow::updateMovesTitle() {
    // "MOVES · Sicilian Defense: Najdorf Variation": the opening the game is in at this move.
    const GameDocument* d = doc();
    QString title = tr("MOVES");
    if (d != nullptr) {
        m_repertoires->loadOpenings();
        if (const auto* names = m_repertoires->openings()) {
            if (const auto* named =
                    names->along(d->game.startPosition(), lineTo(d->game, d->current))) {
                title += QStringLiteral("  ·  ") + QString::fromStdString(named->name);
            }
        }
    }
    m_movesTitle->setText(title);
}

// ---- Repertoires -------------------------------------------------------------------------------

bool MainWindow::addLineAsking(core::Color side, const std::vector<core::Move>& line) {
    auto added = m_repertoires->addLine(side, line, false);
    if (added.outcome == RepertoireStore::Outcome::Conflict) {
        // Our side should have one answer per position; a second one only on purpose.
        core::Position position;
        for (int k = 0; k < added.ply; ++k) {
            position.play(line[static_cast<std::size_t>(k)]);
        }
        QStringList answers;
        for (const auto& move : added.expected) {
            answers << QString::fromStdString(move.san);
        }
        const QString played =
            QString::fromStdString(position.san(line[static_cast<std::size_t>(added.ply)]));
        if (!confirm(this, tr("A second answer?"),
                     tr("Your repertoire already answers this position with %1. Add %2 as "
                        "another answer?")
                         .arg(answers.join(tr(" and ")), played),
                     tr("Add %1").arg(played))) {
            return false;
        }
        added = m_repertoires->addLine(side, line, true);
    }
    if (added.outcome == RepertoireStore::Outcome::NoFile) {
        statusBar()->showMessage(tr("The repertoire is still being read; try again in a moment."),
                                 kStatusMs);
        return false;
    }
    return true;
}

// ---- New files ---------------------------------------------------------------------------------

void MainWindow::newFile(const QString& folder) {
    NewFileDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString error = createFile(
        dialog.name(),
        NewFileDialog::fileText(dialog.name(), dialog.kind(), dialog.side(), dialog.pgn()), folder);
    if (!error.isEmpty()) {
        notice(this, tr("New File"), error);
    }
}

QString MainWindow::createFile(const QString& name, const std::string& text,
                               const QString& folder) {
    // A name that is safe as a file name, and not taken yet.
    QString base = name;
    base.replace(QLatin1Char('/'), QLatin1Char('-')).replace(QLatin1Char(':'), QLatin1Char('-'));
    const QDir into(folder.isEmpty() ? m_library->folder() : folder);
    QString path = into.filePath(base + u".pgn");
    for (int n = 2; QFileInfo::exists(path); ++n) {
        path = into.filePath(QStringLiteral("%1 %2.pgn").arg(base).arg(n));
    }
    QString error = pgnio::write(path, text).result(); // small: written right away
    if (!error.isEmpty()) {
        return error;
    }
    m_library->rescan();
    openFile(path);
    return {};
}

void MainWindow::openGameCopy(const core::Game& game) {
    // A game from a database: a new tab of its own (not tied to a file).
    auto document = std::make_unique<GameDocument>();
    document->game = game;
    addDocument(std::move(document));
    showSection(Section::Board);
}

// ---- Jump to a repertoire's line ---------------------------------------------------------------

namespace {

/// The first node (breadth first: the shortest way) where the game reaches a position.
std::optional<NodeId> findPosition(const Game& game, std::uint64_t position) {
    std::vector<std::pair<NodeId, core::Position>> queue{{Game::root(), game.startPosition()}};
    for (std::size_t i = 0; i < queue.size(); ++i) {
        const auto [node, at] = queue[i];
        if (at.hash() == position) {
            return node;
        }
        for (const NodeId child : game.children(node)) {
            core::Position next = at;
            next.play(game.node(child).move);
            queue.emplace_back(child, next);
        }
    }
    return std::nullopt;
}

} // namespace

void MainWindow::jumpToLine(const QString& path, quint64 position) {
    const RepertoireFile* built = m_repertoires->builtFile(path);
    if (built == nullptr) {
        return;
    }
    // The chapter (game) of the file that has the position.
    for (std::size_t index = 0; index < built->games.size(); ++index) {
        if (!findPosition(built->games[index], position)) {
            continue;
        }
        m_jumpOnOpen = std::pair{path, static_cast<std::uint64_t>(position)};
        openGame(built->file, static_cast<int>(index));
        if (const GameDocument* d = doc(); m_jumpOnOpen && d != nullptr && d->file &&
                                           d->file->path == path &&
                                           std::cmp_equal(d->gameIndex, index)) {
            m_jumpOnOpen.reset(); // it was open already
            goToPosition(position);
        }
        return;
    }
}

namespace {

bool isRepertoire(const GameDocument& d) {
    return d.file && RepertoireStore::kindOf(*d.file) == library::FileKind::Repertoire;
}

} // namespace

std::vector<core::Move> MainWindow::sharedLineOf(const GameDocument& d) const {
    // The whole file as the store has it; not built yet: this chapter alone.
    const RepertoireFile* built = m_repertoires->builtFile(d.file->path);
    const RepertoireFile chapter{d.file, {d.game}, 0};
    return RepertoireStore::trunkOf(built != nullptr ? *built : chapter);
}

NodeId MainWindow::fileStartNode(const GameDocument& d) const {
    if (!isRepertoire(d)) {
        return core::kNoNode;
    }
    // [StartsAt] as this tab has it (it changes here first), else where the lines part.
    const RepertoireFile chapter{d.file, {d.game}, 0};
    const auto chosen = RepertoireStore::chosenStartOf(chapter);
    const std::vector<core::Move> start = chosen ? *chosen : sharedLineOf(d);
    // Along the moves, as far as this chapter has them.
    NodeId node = Game::root();
    for (const core::Move& move : start) {
        NodeId next = d.game.mainChild(node);
        while (next != core::kNoNode && d.game.node(next).move != move) {
            next = d.game.nextSibling(next);
        }
        if (next == core::kNoNode) {
            break;
        }
        node = next;
    }
    return node;
}

void MainWindow::showFileStart() {
    const GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    const NodeId start = fileStartNode(*d);
    m_notation->setStart(start == Game::root() ? core::kNoNode : start, isRepertoire(*d));
}

void MainWindow::goToFileStart() {
    if (const GameDocument* d = doc()) {
        if (const NodeId start = fileStartNode(*d); start != core::kNoNode) {
            showNode(start, false);
        }
    }
}

void MainWindow::setFileStart(NodeId id) {
    const GameDocument* d = doc();
    if (d == nullptr || !isRepertoire(*d) || d->file->path.isEmpty()) {
        return;
    }
    std::vector<core::Move> line;
    if (id != Game::root()) {
        for (const NodeId step : d->game.path(id)) {
            if (step != Game::root()) {
                line.push_back(d->game.node(step).move);
            }
        }
    }
    // Where the lines part is where it starts anyway: no header for that.
    m_repertoires->setStart(d->file->path,
                            line == sharedLineOf(*d) ? std::nullopt : std::optional(line));
}

void MainWindow::goToPosition(std::uint64_t position) {
    const std::optional<core::Move> play = std::exchange(m_playOnJump, std::nullopt);
    if (const GameDocument* d = doc()) {
        if (const auto node = findPosition(d->game, position)) {
            showNode(*node, false);
            if (play) {
                playMoves({*play}); // a gap's move: your answer comes next
            }
        }
    }
}

// ---- Practice ----------------------------------------------------------------------------------

QWidget* MainWindow::buildPracticeCard() {
    auto* frame = card();
    frame->setObjectName(QStringLiteral("practiceCard"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(layout::kCard);
    layout->setSpacing(layout::kCardSpacing);
    m_practiceTitle = label(QString(), "cardTitle");
    layout->addWidget(m_practiceTitle);
    // The status with a tick or a cross, the file's explanation as a quote, then the progress.
    m_practiceMark = new QLabel;
    m_practiceMark->setFixedSize(18, 18);
    m_practiceStatus = label(QString(), "practiceStatus");
    m_practiceStatus->setObjectName(QStringLiteral("practiceStatus"));
    m_practiceStatus->setWordWrap(true);
    auto* statusRow = new QHBoxLayout;
    statusRow->setSpacing(8);
    statusRow->addWidget(m_practiceMark, 0, Qt::AlignTop);
    statusRow->addWidget(m_practiceStatus, 1);
    layout->addLayout(statusRow);
    m_practiceWhy = label(QString(), "quote");
    m_practiceWhy->setObjectName(QStringLiteral("practiceWhy"));
    m_practiceWhy->setWordWrap(true);
    m_practiceWhy->hide();
    layout->addWidget(m_practiceWhy);
    layout->addSpacing(4);
    m_practiceBar = new QProgressBar;
    m_practiceBar->setObjectName(QStringLiteral("practiceBar"));
    m_practiceBar->setTextVisible(false);
    m_practiceBar->setFixedHeight(4);
    layout->addWidget(m_practiceBar);
    m_practiceProgress = label(QString(), "engineStatus");
    m_practiceProgress->setObjectName(QStringLiteral("practiceProgress"));
    layout->addWidget(m_practiceProgress);
    m_practiceSummary = label(QString(), "pvLine");
    m_practiceSummary->setObjectName(QStringLiteral("practiceSummary"));
    m_practiceSummary->setWordWrap(true);
    m_practiceSummary->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(m_practiceSummary, 1);
    auto* buttons = new QHBoxLayout;
    m_practiceAgain = new QPushButton(tr("Again"));
    m_practiceAgain->setObjectName(QStringLiteral("practiceAgain"));
    m_practiceAgain->setProperty("role", "primary");
    m_practiceAgain->setFocusPolicy(Qt::NoFocus);
    connect(m_practiceAgain, &QPushButton::clicked, this,
            [this] { startPractice(m_drillFromHere, m_drillDepth, m_drillMistakes); });
    auto* stop = new QPushButton(tr("Stop"));
    stop->setObjectName(QStringLiteral("practiceStop"));
    stop->setProperty("role", "secondary");
    stop->setFocusPolicy(Qt::NoFocus);
    connect(stop, &QPushButton::clicked, this, &MainWindow::stopPractice);
    buttons->addWidget(m_practiceAgain);
    buttons->addWidget(stop);
    buttons->addStretch();
    layout->addLayout(buttons);
    return frame;
}

void MainWindow::setPracticeStatus(const QString& text, int mood) {
    m_practiceStatus->setText(text);
    const char* name = "";
    if (mood != 0) {
        name = mood > 0 ? "good" : "bad";
    }
    if (m_practiceStatus->property("mood").toString() != QLatin1String(name)) {
        m_practiceStatus->setProperty("mood", QString::fromLatin1(name));
        m_practiceStatus->style()->unpolish(m_practiceStatus);
        m_practiceStatus->style()->polish(m_practiceStatus);
    }
    const auto& c = m_style.colors();
    if (mood == 0) {
        m_practiceMark->clear();
    } else {
        const IconKind icon = mood > 0 ? IconKind::Check : IconKind::Close;
        m_practiceMark->setPixmap(
            makeIcon(icon, mood > 0 ? c.good : c.bad, c.dimmed, 18).pixmap(18, 18));
    }
}

void MainWindow::setPracticeWhy(const QString& text) {
    m_practiceWhy->setText(text);
    m_practiceWhy->setVisible(!text.isEmpty());
}

GameDocument* MainWindow::practiceDocument() const {
    GameDocument* d = doc();
    return d != nullptr && d->id == m_drillDocument ? d : nullptr;
}

void MainWindow::startPractice(bool fromHere, int depth, bool mistakes) {
    const GameDocument* d = doc();
    const std::optional<core::Color> practiceSide =
        d != nullptr ? repertoireSideOf(*d) : std::nullopt;
    if (!practiceSide) {
        return;
    }
    const core::Color side = *practiceSide;
    // Mistakes: only the lines through positions you still get wrong (from the start, any depth).
    const std::unordered_set<std::uint64_t>* focus =
        mistakes ? m_practiceStats->mistakes(side) : nullptr;
    if (mistakes && (focus == nullptr || focus->empty())) {
        statusBar()->showMessage(tr("No mistakes to practise in this file."), 3000);
        return;
    }
    if (!m_drill || m_drillDocument != d->id || mistakes) {
        // From where the file starts (its flag), unless "From Here" says otherwise.
        const NodeId fileStart = fileStartNode(*d);
        m_drillStart = fileStart == core::kNoNode ? Game::root() : fileStart;
        if (fromHere && !mistakes) {
            m_drillStart = d->current;
        }
    }
    m_drillFromHere = fromHere && !mistakes;
    m_drillDepth = mistakes ? 0 : depth;
    m_drillMistakes = mistakes;
    m_drillDocument = d->id;
    ++m_drillRun;
    m_drill = std::make_unique<repertoire::Drill>(
        d->game, side, m_drillStart, m_drillDepth,
        static_cast<std::uint32_t>(QDateTime::currentMSecsSinceEpoch()), focus);
    if (m_drill->lines() == 0) { // the mistakes aren't in this file
        m_drill.reset();
        statusBar()->showMessage(tr("No mistakes to practise in this file."), 3000);
        return;
    }
    // Your side at the bottom; the moves (they'd give the answers away) and the engine off.
    m_board->setFlipped(side == core::Color::Black);
    m_evalBar->setFlipped(side == core::Color::Black);
    m_enginePanel->setPosition(std::nullopt);
    m_shortcuts->setGameKeysEnabled(false);
    m_movesStack->setCurrentIndex(1);
    m_practiceTitle->setText((mistakes ? tr("PRACTICE MISTAKES") : tr("PRACTICE")) +
                             QStringLiteral("  ·  ") + d->title());
    m_practiceSummary->clear();
    m_practiceAgain->hide();
    setPracticeStatus({}, 0);
    setPracticeWhy({});
    showPracticeNode(m_drillStart, false);
    updatePracticeProgress();
    practiceStep();
}

std::optional<core::Color> MainWindow::repertoireSideOf(const GameDocument& d) {
    if (d.wholeRepertoire) {
        return d.wholeRepertoire;
    }
    if (d.file && RepertoireStore::kindOf(*d.file) == library::FileKind::Repertoire) {
        return RepertoireStore::sideOf(*d.file);
    }
    return std::nullopt;
}

void MainWindow::practiceWholeRepertoire(int depth) {
    const GameDocument* d = doc();
    const std::optional<core::Color> side = d != nullptr ? repertoireSideOf(*d) : std::nullopt;
    if (!side) {
        return;
    }
    const BuiltRepertoire* built = m_repertoires->get(*side);
    if (built == nullptr) {
        statusBar()->showMessage(tr("Reading the repertoire…"), 3000);
        return;
    }
    // Every chapter of every file of the side, merged in the background.
    std::vector<core::Game> games;
    for (const RepertoireFile& file : built->files) {
        games.insert(games.end(), file.games.begin(), file.games.end());
    }
    whenReady(this, QtConcurrent::run([games = std::move(games)] {
                  return repertoire::mergeGames(games);
              }),
              [this, side = *side, depth](const core::Game& merged) {
                  // One tab per side: an earlier one gets the repertoire as it is now.
                  GameDocument* existing = nullptr;
                  for (int tab = 0; tab < m_tabs->count(); ++tab) {
                      if (GameDocument* other = docAt(tab);
                          other != nullptr && other->wholeRepertoire == side) {
                          existing = other;
                          m_tabs->setCurrentIndex(tab);
                      }
                  }
                  if (existing != nullptr) {
                      stopPractice();
                      existing->game = merged;
                      existing->current = Game::root();
                      showDocument();
                  } else {
                      auto document = std::make_unique<GameDocument>();
                      document->game = merged;
                      document->wholeRepertoire = side;
                      addDocument(std::move(document));
                  }
                  showSection(Section::Board);
                  startPractice(false, depth);
              });
}

void MainWindow::stopPractice() {
    if (!m_drill) {
        return;
    }
    m_drill.reset();
    ++m_drillRun;
    m_movesStack->setCurrentIndex(0);
    m_shortcuts->setGameKeysEnabled(doc() != nullptr);
    if (doc() != nullptr) {
        showDocument(); // the game as it was, from its own side
    }
}

void MainWindow::playMoveSound(const Game& game, NodeId id) {
    if (id == Game::root()) {
        return;
    }
    const core::Move move = game.node(id).move;
    const core::Position before = game.positionAt(game.parent(id));
    const core::Position after = game.positionAt(id);
    // A capture: a piece on the target square, or a pawn moving aside to an empty one.
    const core::Piece moved = before.pieceAt(move.from);
    const bool captures =
        !before.pieceAt(move.to).isNone() ||
        (moved.type == core::PieceType::Pawn && move.from.file() != move.to.file());
    Sounds::Kind kind = captures ? Sounds::Kind::Capture : Sounds::Kind::Move;
    if (after.inCheck()) {
        kind = Sounds::Kind::Check;
    }
    m_sounds->play(kind);
}

void MainWindow::showPracticeNode(NodeId id, bool animate, std::vector<core::Arrow> arrows) {
    const GameDocument* d = practiceDocument();
    if (d == nullptr) {
        return;
    }
    const auto lastMove = id == Game::root() ? std::nullopt : std::optional(d->game.node(id).move);
    m_board->setPosition(d->game.positionAt(id), lastMove, animate);
    if (animate) {
        playMoveSound(d->game, id);
    }
    m_board->setDrawings(std::move(arrows), {}); // the file's own arrows would give answers away
}

void MainWindow::practiceStep() {
    if (!m_drill) {
        return;
    }
    const int run = m_drillRun;
    const auto later = [this, run](int ms, auto then) {
        QTimer::singleShot(ms, this, [this, run, then] {
            if (m_drill && run == m_drillRun) { // not stopped or restarted meanwhile
                then();
            }
        });
    };
    if (m_drill->lineEnded()) {
        m_drill->nextLine();
        updatePracticeProgress();
        if (m_drill->finished()) {
            if (const GameDocument* done = practiceDocument(); done != nullptr && done->file) {
                m_practiceStats->load(RepertoireStore::sideOf(*done->file)); // mistakes changed
            }
            // The end: what went wrong, to look at again.
            const GameDocument* d = practiceDocument();
            QStringList missed;
            for (const NodeId node : m_drill->missed()) {
                if (d == nullptr) {
                    break;
                }
                missed << moveLabel(d->game, d->game.mainChild(node));
            }
            setPracticeStatus(m_drill->mistakes() == 0 ? tr("Done: no mistakes") : tr("Done"), 1);
            setPracticeWhy({});
            m_practiceSummary->setText(
                missed.isEmpty()
                    ? QString()
                    : tr("To look at again: %1").arg(missed.join(QStringLiteral(", "))));
            m_practiceAgain->show();
            return;
        }
        setPracticeStatus(tr("Line done · next one"), 0);
        later(700, [this] {
            setPracticeWhy({}); // the last explanation belongs to the old line
            showPracticeNode(m_drill->current(), false);
            practiceStep();
        });
        return;
    }
    if (m_drill->yourMove()) {
        later(250, [this] {
            if (m_practiceStatus->property("mood").toString() != u"bad") {
                setPracticeStatus(tr("Your move"), 0);
            }
        });
        return;
    }
    later(450, [this] { // the opponent replies after a moment, so the move can be seen
        if (const auto reply = m_drill->opponentMove()) {
            showPracticeNode(*reply, true);
            practiceStep();
        }
    });
}

void MainWindow::updatePracticeProgress() {
    if (!m_drill) {
        return;
    }
    const int mistakes = m_drill->mistakes();
    m_practiceBar->setRange(0, static_cast<int>(std::max<std::size_t>(m_drill->lines(), 1)));
    m_practiceBar->setValue(static_cast<int>(m_drill->linesDone()));
    m_practiceProgress->setText(
        tr("Line %1 of %2  ·  %3 correct  ·  %4")
            .arg(std::min(m_drill->linesDone() + 1, m_drill->lines()))
            .arg(m_drill->lines())
            .arg(m_drill->correct())
            .arg(mistakes == 1 ? tr("1 mistake") : tr("%1 mistakes").arg(mistakes)));
}

void MainWindow::reloadGamesOf(const QString& path, const std::shared_ptr<PgnFile>& file) {
    for (const auto& d : m_documents) {
        if (!d->file || d->file->path != path) {
            continue;
        }
        d->file = file; // one shared text from now on, so edits from both sides add up
        if (d->gameIndex < 0 || std::cmp_greater_equal(d->gameIndex, file->games.size())) {
            continue;
        }
        const auto& entry = file->games[static_cast<std::size_t>(d->gameIndex)];
        auto game = pgn::parseGame(std::string_view(file->text).substr(entry.offset, entry.length));
        if (!game) {
            continue;
        }
        // Stay on the same move: follow the moves that led there in the reloaded game.
        const std::vector<core::Move> line = lineTo(d->game, d->current);
        d->game = std::move(*game);
        d->current = Game::root();
        for (const core::Move& move : line) {
            const auto children = d->game.children(d->current);
            const auto next = std::find_if(children.begin(), children.end(), [&](NodeId child) {
                return d->game.node(child).move == move;
            });
            if (next == children.end()) {
                break;
            }
            d->current = *next;
        }
        d->modified = false;
        if (d.get() == doc()) {
            showDocument();
        }
    }
    updateTitles();
}

void MainWindow::onMoveMade(core::Move move) {
    GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    if (m_drill) { // practising: the move is an answer, not an edit
        if (!m_drill->yourMove()) {
            showPracticeNode(m_drill->current(), false); // not your turn: take it back
            return;
        }
        const repertoire::Drill::Answer answer = m_drill->play(move);
        if (answer.first && d->file) { // the first answer at a position counts
            m_practiceStats->record(RepertoireStore::sideOf(*d->file), answer.position,
                                    answer.correct);
        }
        if (answer.correct) {
            setPracticeStatus(tr("Correct"), 1);
            showPracticeNode(m_drill->current(), true);
            practiceStep();
        } else {
            const NodeId expected = d->game.mainChild(m_drill->current());
            setPracticeStatus(
                tr("Not quite. The repertoire plays %1").arg(moveLabel(d->game, expected)), -1);
            // Why: the comments on the repertoire's move. They stay until the next mistake or
            // line, so there is time to read them.
            setPracticeWhy(explanation(d->game, expected));
            showPracticeNode(m_drill->current(), false,
                             {{answer.expected.from, answer.expected.to, core::MarkColor::Green}});
        }
        updatePracticeProgress();
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

void MainWindow::playMoves(const std::vector<core::Move>& moves) {
    GameDocument* d = doc();
    if (d == nullptr || moves.empty()) {
        return;
    }
    const std::size_t before = d->game.nodeCount();
    NodeId id = d->current;
    for (const auto& move : moves) {
        id = d->game.addMove(id, move);
    }
    showNode(id, moves.size() == 1);
    if (d->game.nodeCount() != before) {
        setModified();
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
    updateSidebarIcons();
    for (int tab = 0; tab < m_tabs->count(); ++tab) {
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
    if (name == u"Orientation") {
        orientBoard(*d); // a repertoire's side: the board turns with it
    }
}

} // namespace kiebitz::app
