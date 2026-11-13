// MainWindow, part 2: building the window (its pages, the board page and the sidebar), the
// menus, and applying settings. The rest is in MainWindow.cpp.

#include "MainWindow.hpp"
#include "MainWindowKeys.hpp"
#include "board/BoardPage.hpp"
#include "board/BoardTheme.hpp"
#include "board/BoardView.hpp"
#include "board/PieceSet.hpp"
#include "databases/DatabaseManager.hpp"
#include "databases/DatabasesView.hpp"
#include "document/Documents.hpp"
#include "engine/EnginePanel.hpp"
#include "engine/EngineRegistry.hpp"
#include "engine/EngineSettings.hpp"
#include "engine/EnginesView.hpp"
#include "explorer/ExplorerPanel.hpp"
#include "library/Library.hpp"
#include "library/LibraryView.hpp"
#include "notation/MovesCard.hpp"
#include "notation/NotationView.hpp"
#include "panels/GameInfoPanel.hpp"
#include "profiles/LichessLogin.hpp"
#include "profiles/ProfileStore.hpp"
#include "profiles/ProfilesView.hpp"
#include "puzzles/LichessPuzzles.hpp"
#include "puzzles/PuzzleStats.hpp"
#include "puzzles/PuzzlesView.hpp"
#include "repertoire/Practice.hpp"
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
#include "widgets/Card.hpp"
#include "widgets/Message.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMenuBar>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <tuple>
#include <utility>

namespace kiebitz::app {

using core::Game;
using core::NodeId;

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

    // The open games, one tab each.
    m_documents = new Documents(*m_library, m_style, this);
    connect(m_documents, &Documents::shown, this, [this] { showSection(Section::Board); });
    connect(m_documents, &Documents::titlesChanged, this, &MainWindow::updateWindowTitle);
    connect(m_documents, &Documents::reloaded, this, [this](int id) {
        if (const GameDocument* d = doc(); d != nullptr && d->id == id) {
            showDocument();
        }
    });
    connect(m_documents, &Documents::message, this, &MainWindow::showStatus);
    connect(m_documents, &Documents::fileOpened, this, &MainWindow::fileOpened);
    connect(m_documents, &Documents::fileSaved, this, &MainWindow::fileSaved);
    connect(m_documents, &Documents::fileError, this, &MainWindow::fileError);

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
    m_repertoires->setBeforeEdit([this] { m_documents->autosave(); });
    m_repertoires->setOpenText(
        [this](const QString& path) { return m_documents->sharedFile(path); });
    connect(m_repertoires, &RepertoireStore::fileEdited, m_documents, &Documents::reload);
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
    m_library->setFolder(
        QSettings().value(keys::kLibraryFolder, Library::defaultFolder()).toString());

    m_documents->add(std::make_unique<GameDocument>()); // an empty board to play on right away
    showSection(Section::Library);                      // but start in the Library
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
                    [this] { m_documents->add(std::make_unique<GameDocument>()); });
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
    menu->addAction(tr("P&uzzles"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U), this,
                    [this] { showSection(Section::Puzzles); });
    menu->addAction(tr("&Databases"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D), this,
                    [this] { showSection(Section::Databases); });
    menu->addAction(tr("&Profiles"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this,
                    [this] { showSection(Section::Profiles); });
    menu->addAction(tr("&Engines"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), this,
                    [this] { showSection(Section::Engines); });
    menu->addSeparator();
    auto* saveAction = menu->addAction(tr("&Save"), QKeySequence::Save, this, [this] {
        if (GameDocument* d = doc()) {
            m_documents->save(*d);
        }
    });
    auto* saveAsAction = menu->addAction(tr("Save &As…"), QKeySequence::SaveAs, this, [this] {
        if (GameDocument* d = doc()) {
            m_documents->saveAs(*d);
        }
    });
    auto* closeAction = menu->addAction(tr("&Close Tab"), QKeySequence::Close, this, [this] {
        m_documents->close(m_documents->tabs()->currentIndex());
    });
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
        m_boardPage->setFlipped(on);
        refreshClocks();
        m_repertoirePanel->setViewSide(on ? core::Color::Black : core::Color::White);
        if (const GameDocument* d = doc()) {
            m_explorer->setPosition(&d->game, d->current,
                                    on ? core::Color::Black : core::Color::White);
        }
        // Remembered for other games; a game with an [Orientation] keeps its own side.
        if (const GameDocument* d = doc(); d == nullptr || d->game.header("Orientation").empty()) {
            QSettings().setValue(keys::kFlipped, on);
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
    m_customColorsAction->setChecked(current == keys::kCustomTheme);
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
            const bool inUse = now.name == keys::kCustomTheme ? action == m_customColorsAction
                                                              : action->text() == now.name;
            action->setChecked(inUse);
        }
        emit Settings::instance().changed(QString::fromLatin1(Settings::kTheme));
        return;
    }
    QSettings settings;
    settings.setValue(keys::kCustomLight, light);
    settings.setValue(keys::kCustomDark, dark);
    Settings::instance().setValue(Settings::kTheme, QString(keys::kCustomTheme));
    applySetting(QString::fromLatin1(Settings::kTheme)); // new colours, if Custom was on already
}

void MainWindow::chooseLibraryFolder() {
    const QString folder =
        QFileDialog::getExistingDirectory(this, tr("Library Folder"), m_library->folder());
    if (!folder.isEmpty()) {
        m_library->setFolder(folder);
        QSettings().setValue(keys::kLibraryFolder, folder);
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

void MainWindow::showAbout() {
    notice(this, tr("About Kiebitz"),
           tr("Kiebitz %1: a fast native chess app for opening preparation.")
               .arg(QApplication::applicationVersion()),
           tr("Licensed under the GNU GPL v3. Piece sets: cburnett and merida (GPLv2+), "
              "chessnut (Apache 2.0), fantasy (MIT), via Lichess."));
}

// ---- Layout ------------------------------------------------------------------------------------

void MainWindow::buildLayout() {
    connect(m_documents->tabs(), &QTabBar::currentChanged, this, &MainWindow::updatePage);
    auto* newTab = new QToolButton;
    newTab->setObjectName(QStringLiteral("newTabButton"));
    newTab->setText(QStringLiteral("+"));
    newTab->setToolTip(tr("New tab (⌘T)"));
    newTab->setFocusPolicy(Qt::NoFocus);
    connect(newTab, &QToolButton::clicked, this,
            [this] { m_documents->add(std::make_unique<GameDocument>()); });
    auto* tabRow = new QWidget;
    tabRow->setObjectName(QStringLiteral("tabRow"));
    auto* tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(0, 0, 8, 0);
    tabLayout->setSpacing(4);
    tabLayout->addWidget(m_documents->tabs());
    tabLayout->addWidget(newTab);
    tabLayout->addStretch();

    buildPages();

    // The board page: the game tabs over the board and its panels.
    auto* boardPage = new QWidget;
    auto* boardLayout = new QVBoxLayout(boardPage);
    boardLayout->setContentsMargins(0, 0, 0, 0);
    boardLayout->setSpacing(0);
    boardLayout->addWidget(tabRow);
    boardLayout->addWidget(buildBoardPage(), 1);

    m_pages = new QStackedWidget; // in Section order
    m_pages->addWidget(boardPage);
    m_pages->addWidget(m_libraryView);
    m_pages->addWidget(m_puzzlesView);
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

    auto* central = new QWidget;
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildSidebar());
    layout->addWidget(m_pages, 1);
    setCentralWidget(central);

    connect(&m_style, &AppStyle::changed, this, [this] {
        updateIcons();
        refreshNotation();
    });
    updateIcons();
}

void MainWindow::buildPages() {
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
    connect(m_libraryView, &LibraryView::positionRequested, this,
            [this](const QString& path, const std::vector<core::Move>& line) {
                core::Position position;
                for (const core::Move& played : line) {
                    position.play(played);
                }
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
    // A file or folder moved or went in the Library: its open tabs follow.
    connect(m_libraryView, &LibraryView::pathMoved, m_documents, &Documents::movePath);
    connect(m_libraryView, &LibraryView::pathRemoved, m_documents, &Documents::closeUnder);
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

    // Puzzles: the Lichess puzzle database and the Library's puzzle files.
    m_lichessPuzzles = new LichessPuzzles({}, this);
    m_puzzleStats = new PuzzleStats({}, this);
    m_puzzlesView =
        new PuzzlesView(*m_lichessPuzzles, *m_puzzleStats, *m_library, *m_sounds, m_style);
    connect(m_puzzlesView, &PuzzlesView::analyseRequested, this, &MainWindow::openGameCopy);
    // The Lichess puzzles start around your best rating: the highest of your main profile's
    // accounts and its FIDE ratings.
    m_puzzlesView->setRatingSource([this] {
        int highest = 0;
        for (const Profile& profile : m_profiles->profiles()) {
            if (!profile.main) {
                continue;
            }
            for (const Account& account : profile.accounts) {
                for (const auto& [speed, rating] : account.ratings) {
                    highest = std::max(highest, rating);
                }
            }
            if (profile.fide) {
                highest = std::max(
                    {highest, profile.fide->standard, profile.fide->rapid, profile.fide->blitz});
            }
        }
        return highest;
    });
    connect(m_profiles, &ProfileStore::changed, m_puzzlesView, &PuzzlesView::followRating);
    connect(m_libraryView, &LibraryView::puzzlesRequested, this, [this](const QString& path) {
        m_puzzlesView->setSource(path); // before the page opens the puzzles of last time
        showSection(Section::Puzzles);
    });

    m_enginesView = new EnginesView(*m_engineRegistry, m_style);
    m_enginesView->setObjectName(QStringLiteral("enginesView"));
}

QWidget* MainWindow::buildSidebar() {
    // One icon per section.
    auto* sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(52);
    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(0, 8, 0, 8);
    sideLayout->setSpacing(4);
    m_sidebar = new QButtonGroup(this);
    const auto addButton = [this](Section section, const char* name, const QString& tip) {
        auto* button = new QToolButton;
        button->setObjectName(QString::fromLatin1(name));
        button->setProperty("role", "sideNav");
        button->setToolTip(tip);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setIconSize(QSize(22, 22));
        button->setFixedSize(52, 44);
        m_sidebar->addButton(button, static_cast<int>(section));
        return button;
    };
    for (const auto& [section, name, tip] :
         {std::tuple{Section::Board, "sideBoard", tr("Board (⌘1)")},
          std::tuple{Section::Library, "sideLibrary", tr("Library (⌘L)")},
          std::tuple{Section::Puzzles, "sidePuzzles", tr("Puzzles (⇧⌘U)")},
          std::tuple{Section::Databases, "sideDatabases", tr("Databases (⇧⌘D)")},
          std::tuple{Section::Profiles, "sideProfiles", tr("Profiles (⇧⌘P)")},
          std::tuple{Section::Engines, "sideEngines", tr("Engines (⇧⌘E)")}}) {
        sideLayout->addWidget(addButton(section, name, tip));
    }
    sideLayout->addStretch();
    // Settings: at the bottom, apart from the sections.
    sideLayout->addWidget(addButton(Section::Settings, "sideSettings", tr("Settings (⌘,)")));
    m_sidebar->setExclusive(true);
    connect(m_sidebar, &QButtonGroup::idClicked, this,
            [this](int id) { showSection(static_cast<Section>(id)); });
    connect(m_sidebar, &QButtonGroup::buttonToggled, this, &MainWindow::updateSidebarIcons);
    return sidebar;
}

QWidget* MainWindow::buildBoardPage() {
    m_boardPage = new BoardPage;
    m_board = m_boardPage->board();
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
    connect(m_boardPage, &BoardPage::wheelStep, this, [this](int step) {
        if (step < 0) {
            goBack();
        } else {
            goForward();
        }
    });

    // The moves, with the comment box and the buttons to step through them.
    m_moves = new MovesCard(m_style);
    m_notation = m_moves->notation();
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
    connect(m_moves, &MovesCard::commentEdited, this, &MainWindow::onCommentEdited);
    connect(m_moves, &MovesCard::startRequested, this, &MainWindow::goToStart);
    connect(m_moves, &MovesCard::backRequested, this, &MainWindow::goBack);
    connect(m_moves, &MovesCard::forwardRequested, this, &MainWindow::goForward);
    connect(m_moves, &MovesCard::endRequested, this, &MainWindow::goToEnd);
    connect(m_moves, &MovesCard::continuationClicked, this, [this](NodeId child) {
        markContinuationsLearned();
        showNode(child, true);
    });

    // Practice: its card takes the moves' place while it runs.
    m_practice = new Practice(
        *m_practiceStats, *m_board, *m_sounds, m_style,
        [this]() -> const GameDocument* { return doc(); }, this);
    connect(m_practice, &Practice::started, this, &MainWindow::onPracticeStarted);
    connect(m_practice, &Practice::stopped, this, &MainWindow::onPracticeStopped);
    connect(m_practice, &Practice::againRequested, this, [this] {
        startPractice(m_practice->fromHere(), m_practice->depth(), m_practice->mistakesOnly());
    });
    connect(m_practice, &Practice::message, this,
            [this](const QString& text) { showStatus(text, 3000); });

    m_boardPage->setSide(m_moves, m_practice->card(), buildPanelsCard());
    return m_boardPage;
}

QWidget* MainWindow::buildPanelsCard() {
    auto* frame = ui::card();
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
                if (m_practice->active()) { // a late report: it would give the answers away
                    m_boardPage->setAnalysis({}, std::nullopt, false);
                    return;
                }
                m_boardPage->setAnalysis(best, score, running);
            });
    panels->addTab(m_enginePanel, tr("Engine"));
    m_explorer = new ExplorerPanel(*m_databases, *m_profiles, *m_repertoires, *m_lichess, m_style);
    connect(m_explorer, &ExplorerPanel::movePlayed, this, [this](core::Move move) {
        if (!m_practice->active()) { // in practice the explorer would give the answers away
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
            showStatus(side == core::Color::White ? tr("Added to your White repertoire")
                                                  : tr("Added to your Black repertoire"),
                       keys::kStatusMs);
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
    m_boardPage->restoreLayout(settings);
    m_boardPage->setFlipped(settings.value(keys::kFlipped, false).toBool());
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
        m_board->setTheme(theme == keys::kCustomTheme
                              ? BoardTheme::custom(stored.value(keys::kCustomLight).value<QColor>(),
                                                   stored.value(keys::kCustomDark).value<QColor>())
                              : BoardTheme::preset(theme));
        m_libraryView->setBoardLook(m_board->theme(), m_board->pieceSetName());
        m_puzzlesView->setBoardLook(m_board->theme(), m_board->pieceSetName());
        // The menus' ticks (at the start the menus aren't there yet: they read the board).
        for (auto* action : m_themeGroup != nullptr ? m_themeGroup->actions() : QList<QAction*>{}) {
            action->setChecked(theme == keys::kCustomTheme
                                   ? action == m_customColorsAction
                                   : action->text() == m_board->theme().name);
        }
    } else if (is(Settings::kPieces)) {
        m_board->setPieceSet(settings.string(Settings::kPieces));
        m_libraryView->setBoardLook(m_board->theme(), m_board->pieceSetName());
        m_puzzlesView->setBoardLook(m_board->theme(), m_board->pieceSetName());
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
        m_puzzlesView->setBoardOptions(options);
        if (m_coordinatesAction != nullptr) {
            const QSignalBlocker block(m_coordinatesAction);
            m_coordinatesAction->setChecked(m_board->showCoordinates());
        }
    } else if (is(Settings::kNotationRows)) {
        m_moves->setRows(settings.flag(Settings::kNotationRows));
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

} // namespace kiebitz::app
