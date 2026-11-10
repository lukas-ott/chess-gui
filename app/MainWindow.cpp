#include "MainWindow.hpp"

#include "board/BoardTheme.hpp"
#include "board/BoardView.hpp"
#include "board/PieceSet.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>

namespace chessgui::app {
namespace {

using namespace Qt::StringLiterals;

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

/// Escapes text for the move list's HTML.
QString html(const std::string& text) {
    return QString::fromStdString(text).toHtmlEscaped();
}

} // namespace

MainWindow::MainWindow(AppStyle& style, QWidget* parent) : QMainWindow(parent), m_style(style) {
    setWindowTitle(QStringLiteral("ChessGui"));
    buildLayout();
    restoreState(); // before the menus, which show the board's current settings
    buildMenus();
    showNode(m_current, false);
    m_board->setFocus();
}

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
    connect(shortcut, &QShortcut::activated, action, [action] {
        if (action->isEnabled()) {
            action->trigger();
        }
    });
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* openAction = fileMenu->addAction(tr("&Open…"));
    openAction->setShortcut(QKeySequence::Open);
    openAction->setEnabled(false); // M3: PGN viewer/editor
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(tr("&Quit"), qApp, &QApplication::quit);
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setMenuRole(QAction::QuitRole);

    auto* gameMenu = menuBar()->addMenu(tr("&Game"));
    auto* newAction = gameMenu->addAction(tr("&New Game"), this, &MainWindow::newGame);
    newAction->setShortcut(QKeySequence::New);
    gameMenu->addSeparator();
    m_backAction = gameMenu->addAction(tr("&Back"), this, &MainWindow::goBack);
    bindPlainKey(m_backAction, Qt::Key_Left);
    m_forwardAction = gameMenu->addAction(tr("&Forward"), this, &MainWindow::goForward);
    bindPlainKey(m_forwardAction, Qt::Key_Right);
    m_startAction = gameMenu->addAction(tr("Go to &Start"), this, &MainWindow::goToStart);
    bindPlainKey(m_startAction, Qt::Key_Home);
    m_endAction = gameMenu->addAction(tr("Go to &End"), this, &MainWindow::goToEnd);
    bindPlainKey(m_endAction, Qt::Key_End);

    // ↑ ↓: choose between continuations at a branch point; elsewhere go to the start / end.
    // Plain window shortcuts for the same macOS reason as bindPlainKey().
    const auto* up = new QShortcut(Qt::Key_Up, this);
    connect(up, &QShortcut::activated, this, [this] { stepChoiceOrJump(-1); });
    const auto* down = new QShortcut(Qt::Key_Down, this);
    connect(down, &QShortcut::activated, this, [this] { stepChoiceOrJump(+1); });

    buildViewMenu(menuBar()->addMenu(tr("&View")));

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAction = helpMenu->addAction(tr("&About ChessGui"), this, &MainWindow::showAbout);
    aboutAction->setMenuRole(QAction::AboutRole);
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
    QSettings settings;
    settings.setValue(kThemeKey, kCustomTheme);
    settings.setValue(kCustomLightKey, light);
    settings.setValue(kCustomDarkKey, dark);
}

// ---- Layout ------------------------------------------------------------------------------------

void MainWindow::buildLayout() {
    auto* documentTabs = new QTabBar;
    documentTabs->setObjectName(QStringLiteral("documentTabs"));
    documentTabs->setDocumentMode(true);
    documentTabs->setDrawBase(false);
    documentTabs->setExpanding(false);
    documentTabs->setFocusPolicy(Qt::NoFocus);
    documentTabs->addTab(tr("Analysis"));

    m_board = new BoardView;
    connect(m_board, &BoardView::moveMade, this, &MainWindow::onMoveMade);
    connect(
        m_board, &BoardView::drawingsChanged, this,
        [this](const std::vector<core::Arrow>& arrows, const std::vector<core::SquareMark>& marks) {
            m_game.setArrows(m_current, arrows);
            m_game.setSquareMarks(m_current, marks);
        });

    m_sideSplitter = new QSplitter(Qt::Vertical);
    m_sideSplitter->addWidget(buildMovesCard());
    m_sideSplitter->addWidget(buildPanelsCard());
    m_sideSplitter->setChildrenCollapsible(false);
    m_sideSplitter->setStretchFactor(0, 3);
    m_sideSplitter->setStretchFactor(1, 2);

    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->addWidget(m_board);
    m_mainSplitter->addWidget(m_sideSplitter);
    m_mainSplitter->setChildrenCollapsible(false);
    // When the window grows, the board gets the extra room; the side column keeps its width.
    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 0);
    m_sideSplitter->setMinimumWidth(340);

    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(8, 4, 10, 10);
    contentLayout->addWidget(m_mainSplitter);

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(documentTabs);
    layout->addWidget(content, 1);
    setCentralWidget(central);

    connect(&m_style, &AppStyle::changed, this, [this] {
        updateIcons();
        updateMoveList();
    });
    updateIcons();
}

QWidget* MainWindow::buildMovesCard() {
    auto* frame = card();
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    layout->addWidget(label(tr("MOVES"), "cardTitle"));

    m_moveList = new QTextBrowser;
    m_moveList->setObjectName(QStringLiteral("moveList"));
    m_moveList->setOpenLinks(false);
    m_moveList->setFocusPolicy(Qt::NoFocus); // keep ← → for moving through the game
    m_moveList->setFrameShape(QFrame::NoFrame);
    connect(m_moveList, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        bool ok = false;
        const auto id = static_cast<core::NodeId>(url.fragment().toUInt(&ok));
        if (ok) {
            showNode(id, false);
        }
    });
    layout->addWidget(m_moveList, 1);

    // Start / back / forward / end, as in most chess GUIs; each mirrors a Game menu action.
    // Their icons are set (and recoloured) in updateIcons().
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
    // At a branch point: the continuations to choose from (↑ ↓ to choose, → to play).
    m_forkPanel = new QWidget;
    m_forkLayout = new QVBoxLayout(m_forkPanel);
    m_forkLayout->setContentsMargins(0, 0, 0, 0);
    m_forkLayout->setSpacing(4);
    m_forkPanel->hide();
    layout->addWidget(m_forkPanel);

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
    panels->addTab(emptyState(tr("Game info"),
                              tr("Coming in M3 with PGN files: players, event, date, result.")),
                   tr("Info"));
    layout->addWidget(panels);
    return frame;
}

void MainWindow::restoreState() {
    const QSettings settings;
    if (!restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray())) {
        resize(1200, 780);
    }
    m_mainSplitter->restoreState(
        settings.value(QStringLiteral("window/mainSplitter")).toByteArray());
    m_sideSplitter->restoreState(
        settings.value(QStringLiteral("window/sideSplitter")).toByteArray());

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
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    fitBoardColumn();
}

void MainWindow::fitBoardColumn() {
    // The board is square, so a board column wider than it is tall only adds empty space.
    // Give it exactly its height and the rest to the side column.
    const QList<int> sizes = m_mainSplitter->sizes();
    if (sizes.size() != 2) {
        return;
    }
    const int total = sizes[0] + sizes[1];
    const int side = std::max(m_sideSplitter->minimumWidth(), total - m_mainSplitter->height());
    m_mainSplitter->setSizes({total - side, side});
}

void MainWindow::closeEvent(QCloseEvent* event) {
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

// ---- Game and navigation -----------------------------------------------------------------------

void MainWindow::showNode(core::NodeId id, bool animate) {
    m_current = id;
    const core::Node& node = m_game.node(id);
    const auto lastMove = id == core::Game::root() ? std::nullopt : std::optional(node.move);
    m_board->setPosition(m_game.positionAt(id), lastMove, animate);
    m_board->setDrawings(node.arrows, node.squareMarks);
    m_choice = 0; // the main continuation, until the user picks another
    updateFork();
    updateNavigation();
    updateMoveList();
}

void MainWindow::stepChoiceOrJump(int step) {
    const auto children = m_game.children(m_current);
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
    const auto children = m_game.children(m_current);
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
        const core::NodeId child = children[i];
        moves.push_back(m_game.node(child).move);
        const bool white = m_game.mover(child) == core::Color::White;
        auto* button = new QPushButton(QStringLiteral("%1%2 %3")
                                           .arg(m_game.moveNumber(child))
                                           .arg(white ? QStringLiteral(".") : QStringLiteral("…"))
                                           .arg(QString::fromStdString(m_game.node(child).san)));
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

void MainWindow::onMoveMade(core::Move move) {
    showNode(m_game.addMove(m_current, move), true);
}

void MainWindow::newGame() {
    m_game = core::Game();
    showNode(core::Game::root(), false);
}

void MainWindow::goBack() {
    if (m_current != core::Game::root()) {
        showNode(m_game.parent(m_current), false);
    }
}

void MainWindow::goForward() {
    // Follows the chosen continuation at a branch point (the main line otherwise).
    const auto children = m_game.children(m_current);
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
    showNode(core::Game::root(), false);
}

void MainWindow::goToEnd() {
    core::NodeId last = m_current;
    while (m_game.mainChild(last) != core::kNoNode) {
        last = m_game.mainChild(last);
    }
    showNode(last, false);
}

void MainWindow::updateNavigation() {
    const bool atStart = m_current == core::Game::root();
    const bool atEnd = m_game.mainChild(m_current) == core::kNoNode;
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
    m_startButton->setIcon(makeIcon(IconKind::First, c.text, c.dimmed));
    m_backButton->setIcon(makeIcon(IconKind::Previous, c.text, c.dimmed));
    m_forwardButton->setIcon(makeIcon(IconKind::Next, c.text, c.dimmed));
    m_endButton->setIcon(makeIcon(IconKind::Last, c.text, c.dimmed));
}

void MainWindow::updateMoveList() {
    // A simple read-only move list: main line, variations in brackets, the current move
    // highlighted, every move a link to its node. M3 replaces it with the full notation editor.
    const auto& c = m_style.colors();
    QString text;
    const auto writeMove = [&](core::NodeId id, bool forceNumber) {
        const core::Node& node = m_game.node(id);
        const bool white = m_game.mover(id) == core::Color::White;
        if (white || forceNumber) {
            text += QStringLiteral("<span style='color:%1'>%2%3</span>&nbsp;")
                        .arg(c.muted.name())
                        .arg(m_game.moveNumber(id))
                        .arg(white ? QStringLiteral(".") : QStringLiteral("…"));
        }
        QString san = html(node.san);
        for (const auto nag : node.nags) {
            san += QString::fromUtf8(core::nagSymbol(nag).data(),
                                     static_cast<qsizetype>(core::nagSymbol(nag).size()));
        }
        const bool current = id == m_current;
        QColor tint = c.accent;
        tint.setAlphaF(0.22F);
        const QString style =
            current
                ? QStringLiteral("background:%1;color:%2;font-weight:600")
                      .arg(tint.name(QColor::HexArgb), c.accent.name())
                : QStringLiteral("color:%1").arg((m_game.isMainLine(id) ? c.text : c.muted).name());
        text += QStringLiteral("<a name='n%1' href='#%1' style='%2'>%3</a> ")
                    .arg(id)
                    .arg(style, current ? QStringLiteral("&#8202;%1&#8202;").arg(san) : san);
        if (!node.comment.empty()) {
            text += QStringLiteral("<i style='color:%1'>%2</i> ")
                        .arg(c.muted.name(), html(node.comment));
        }
    };
    // Writes the line that continues after `parent`, with each alternative in brackets.
    std::function<void(core::NodeId, bool)> writeLine = [&](core::NodeId parent, bool number) {
        for (core::NodeId node = m_game.mainChild(parent); node != core::kNoNode;
             node = m_game.mainChild(node)) {
            writeMove(node, number);
            number = false;
            for (core::NodeId alt = m_game.nextSibling(node); alt != core::kNoNode;
                 alt = m_game.nextSibling(alt)) {
                text += QStringLiteral("<span style='color:%1'>(</span>").arg(c.muted.name());
                writeMove(alt, true);
                writeLine(alt, false);
                if (text.endsWith(QLatin1Char(' '))) {
                    text.chop(1); // "(5. d3)", not "(5. d3 )"
                }
                text += QStringLiteral("<span style='color:%1'>)</span> ").arg(c.muted.name());
                number = true; // after a variation, the main line needs its number again
            }
        }
    };
    writeLine(core::Game::root(), true);
    if (text.isEmpty()) {
        text = QStringLiteral("<span style='color:%1'>%2</span>")
                   .arg(c.muted.name(), tr("Play a move on the board to start."));
    }
    m_moveList->document()->setDefaultStyleSheet(
        QStringLiteral("a { text-decoration: none; color: %1; }").arg(c.text.name()));
    m_moveList->setHtml(
        QStringLiteral("<div style='font-size:14px; line-height:170%'>%1</div>").arg(text));
    if (m_current != core::Game::root()) {
        m_moveList->scrollToAnchor(QStringLiteral("n%1").arg(m_current));
    }
}

} // namespace chessgui::app
