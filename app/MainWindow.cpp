#include "MainWindow.hpp"

#include "MainWindowKeys.hpp"
#include "board/BoardPage.hpp"
#include "board/BoardView.hpp"
#include "document/Documents.hpp"
#include "engine/EnginePanel.hpp"
#include "explorer/ExplorerPanel.hpp"
#include "kiebitz/core/annotations.hpp"
#include "kiebitz/repertoire/drill.hpp"
#include "library/LibraryView.hpp"
#include "library/NewFileDialog.hpp"
#include "notation/MovesCard.hpp"
#include "notation/NotationView.hpp"
#include "notify/Notifications.hpp"
#include "panels/GameInfoPanel.hpp"
#include "puzzles/PuzzlesView.hpp"
#include "repertoire/Practice.hpp"
#include "repertoire/PracticeStats.hpp"
#include "repertoire/RepertoirePanel.hpp"
#include "repertoire/RepertoireStore.hpp"
#include "settings/Shortcuts.hpp"
#include "sound/Sounds.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"
#include "training/TrainingHistory.hpp"
#include "training/TrainingSession.hpp"
#include "training/TrainingView.hpp"
#include "util/WhenReady.hpp"
#include "widgets/Message.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextCursor>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <optional>
#include <utility>

namespace kiebitz::app {
namespace {

using core::Game;
using core::NodeId;

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

bool isRepertoire(const GameDocument& d) {
    return d.file && RepertoireStore::kindOf(*d.file) == library::FileKind::Repertoire;
}

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

// ---- The window --------------------------------------------------------------------------------

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    m_boardPage->fit();
    // Once more when the layout has settled: right now the splitters still have their old size.
    QMetaObject::invokeMethod(m_boardPage, &BoardPage::fit, Qt::QueuedConnection);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    m_training->stop(); // a session's time so far goes into the log
    // No questions when quitting either: games from files are saved (as autosave would), new
    // games that aren't in a file just go.
    if (!m_documents->saveAll()) {
        event->ignore(); // couldn't be saved: stay open (it said why)
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    m_boardPage->saveLayout(settings);
    event->accept();
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

void MainWindow::showStatus(const QString& text, int ms) {
    if (text.isEmpty()) {
        statusBar()->clearMessage();
    } else {
        statusBar()->showMessage(text, ms);
    }
}

// ---- Pages and tabs ----------------------------------------------------------------------------

GameDocument* MainWindow::doc() const {
    // Only the board page has a current game (menus act on it); the other pages have none.
    return m_section == Section::Board ? m_documents->current() : nullptr;
}

void MainWindow::showSection(Section section) {
    m_section = section;
    if (auto* button = m_sidebar->button(static_cast<int>(section))) {
        button->setChecked(true);
    }
    updatePage();
    if (section == Section::Training) {
        m_trainingView->activate();
    }
    if (section == Section::Puzzles) {
        m_puzzlesView->activate(); // the puzzles of last time, opened when first wanted
        m_puzzlesView->board()->setFocus();
    }
}

void MainWindow::updatePage() {
    m_notationRefresh.stop();
    const GameDocument* d = doc();
    if (m_practice->active() && (d == nullptr || d->id != m_practice->documentId())) {
        m_practice->stop(); // another tab or page: practice ends
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
        m_boardPage->fit();
    }
    m_documents->updateTitles();
}

void MainWindow::updateSidebarIcons() {
    const auto& c = m_style.colors();
    for (auto* button : m_sidebar->buttons()) {
        const auto section = static_cast<Section>(m_sidebar->id(button));
        IconKind kind = IconKind::Board;
        if (section == Section::Library) {
            kind = IconKind::Library;
        } else if (section == Section::Training) {
            kind = IconKind::Stopwatch;
        } else if (section == Section::Puzzles) {
            kind = IconKind::Puzzle;
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

void MainWindow::updateWindowTitle() {
    const GameDocument* d = doc();
    if (d == nullptr) {
        QString page = tr("Library");
        if (m_section == Section::Training) {
            page = tr("Training");
        } else if (m_section == Section::Puzzles) {
            page = tr("Puzzles");
        } else if (m_section == Section::Engines) {
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

void MainWindow::setModified() {
    if (GameDocument* d = doc()) {
        m_documents->setModified(*d);
    }
}

void MainWindow::updateIcons() {
    updateSidebarIcons();
    m_documents->restyle();
    m_moves->restyle();
}

// ---- Opening -----------------------------------------------------------------------------------

void MainWindow::openFile(const QString& path) {
    m_documents->openFile(path);
}

void MainWindow::openGame(const std::shared_ptr<PgnFile>& file, int index) {
    m_documents->openGame(file, index);
}

void MainWindow::saveCurrentAs(const QString& path) {
    if (GameDocument* d = doc()) {
        m_documents->saveAs(*d, path);
    }
}

void MainWindow::openGameCopy(const core::Game& game) {
    // A game from a database: a new tab of its own (not tied to a file).
    auto document = std::make_unique<GameDocument>();
    document->game = game;
    m_documents->add(std::move(document));
    showSection(Section::Board);
}

void MainWindow::newFile(const QString& folder) {
    NewFileDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString error = m_documents->createFile(
        dialog.name(),
        NewFileDialog::fileText(dialog.name(), dialog.kind(), dialog.side(), dialog.pgn()), folder);
    if (!error.isEmpty()) {
        notice(this, tr("New File"), error);
    }
}

// ---- Clipboard ---------------------------------------------------------------------------------

void MainWindow::copyPgn() {
    if (const GameDocument* d = doc()) {
        QApplication::clipboard()->setText(QString::fromStdString(pgn::writeGame(d->game)));
        showStatus(tr("Game copied as PGN"), keys::kStatusMs);
    }
}

void MainWindow::copyFen() {
    if (const GameDocument* d = doc()) {
        QApplication::clipboard()->setText(
            QString::fromStdString(d->game.positionAt(d->current).fen()));
        showStatus(tr("Position copied as FEN"), keys::kStatusMs);
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
    m_documents->openText(QApplication::clipboard()->text());
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
    m_documents->add(std::move(document));
}

// ---- Showing the game --------------------------------------------------------------------------

void MainWindow::orientBoard(const GameDocument& document) {
    const QString side = toQString(document.game.header("Orientation"));
    bool flipped = QSettings().value(keys::kFlipped, false).toBool();
    if (side.compare(u"black", Qt::CaseInsensitive) == 0) {
        flipped = true;
    } else if (side.compare(u"white", Qt::CaseInsensitive) == 0) {
        flipped = false;
    }
    if (m_boardPage->isFlipped() != flipped) {
        m_boardPage->setFlipped(flipped);
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
    m_info->setGame(&d->game, isRepertoire(*d));
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
    if (d == nullptr || m_practice->active()) { // while practising, the practice drives the board
        return;
    }
    d->current = id;
    const core::Node& node = d->game.node(id);
    const auto lastMove = id == Game::root() ? std::nullopt : std::optional(node.move);
    m_board->setPosition(d->game.positionAt(id), lastMove, animate);
    if (animate) {
        m_sounds->playMove(d->game, id); // a move played, or one step forward
    }
    m_board->setDrawings(node.arrows, node.squareMarks);
    refreshClocks();
    m_notation->setCurrent(id);
    m_moves->showComment(d->game, id);
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

void MainWindow::refreshClocks() {
    const GameDocument* d = doc();
    std::array<QString, 2> times; // White, Black: the last clock of each up to here
    bool any = false;
    if (d != nullptr && !m_practice->active()) {
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
    m_boardPage->setClocks(any ? times : std::array<QString, 2>{});
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
    m_moves->setTitle(title);
}

void MainWindow::onMoveMade(core::Move move) {
    GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    if (m_practice->active()) { // practising: the move is an answer, not an edit
        m_practice->answer(move);
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
    QSettings().setValue(keys::kContinuationsLearned, true);
}

void MainWindow::updateFork() {
    // A one-time hint: once the user has picked a continuation, the arrows and the
    // highlighted choice explain themselves (as on Lichess), so it never shows again.
    const bool hint = !QSettings().value(keys::kContinuationsLearned, false).toBool();
    const GameDocument* d = doc();
    if (d == nullptr) {
        m_moves->showContinuations(nullptr, Game::root(), m_choice, hint);
        m_board->setCandidates({}, -1);
        return;
    }
    m_moves->showContinuations(&d->game, d->current, m_choice, hint);
    const auto children = d->game.children(d->current);
    if (children.size() < 2) {
        m_board->setCandidates({}, -1);
        return;
    }
    std::vector<core::Move> moves;
    moves.reserve(children.size());
    for (const NodeId child : children) {
        moves.push_back(d->game.node(child).move);
    }
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
    m_moves->setNavigation(atStart, atEnd);
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
        showStatus(tr("The repertoire is still being read; try again in a moment."),
                   keys::kStatusMs);
        return false;
    }
    return true;
}

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

std::optional<core::Color> MainWindow::repertoireSideOf(const GameDocument& d) {
    if (d.wholeRepertoire) {
        return d.wholeRepertoire;
    }
    if (isRepertoire(d)) {
        return RepertoireStore::sideOf(*d.file);
    }
    return std::nullopt;
}

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
    const std::vector<core::Move> line =
        id == Game::root() ? std::vector<core::Move>{} : lineTo(d->game, id);
    // Where the lines part is where it starts anyway: no header for that.
    m_repertoires->setStart(d->file->path,
                            line == sharedLineOf(*d) ? std::nullopt : std::optional(line));
}

// ---- Practice ----------------------------------------------------------------------------------

void MainWindow::startPractice(bool fromHere, int depth, bool mistakes) {
    const GameDocument* d = doc();
    const std::optional<core::Color> side = d != nullptr ? repertoireSideOf(*d) : std::nullopt;
    if (!side) {
        return;
    }
    m_practice->start(*d, *side, fileStartNode(*d), fromHere, depth, mistakes);
}

void MainWindow::practiceWholeRepertoire(int depth) {
    const GameDocument* d = doc();
    const std::optional<core::Color> side = d != nullptr ? repertoireSideOf(*d) : std::nullopt;
    if (!side) {
        return;
    }
    const BuiltRepertoire* built = m_repertoires->get(*side);
    if (built == nullptr) {
        showStatus(tr("Reading the repertoire…"), 3000);
        return;
    }
    // Every chapter of every file of the side, merged in the background.
    std::vector<core::Game> games;
    for (const RepertoireFile& file : built->files) {
        games.insert(games.end(), file.games.begin(), file.games.end());
    }
    whenReady(
        this,
        QtConcurrent::run([games = std::move(games)] { return repertoire::mergeGames(games); }),
        [this, side = *side, depth](const core::Game& merged) {
            // One tab per side: an earlier one gets the repertoire as it is now.
            if (GameDocument* existing = m_documents->select(
                    [side](const GameDocument& other) { return other.wholeRepertoire == side; })) {
                m_practice->stop();
                existing->game = merged;
                existing->current = Game::root();
            } else {
                auto document = std::make_unique<GameDocument>();
                document->game = merged;
                document->wholeRepertoire = side;
                m_documents->add(std::move(document));
            }
            showSection(Section::Board); // shows it
            startPractice(false, depth);
        });
}

void MainWindow::onTrainingPart(training::Part part) {
    if (m_training->session() != nullptr && m_training->session()->index() == 0) {
        // macOS asks you the first time; if they're off, say where to switch them on.
        notifications::requestPermission([this](const QString& /*why*/) {
            showStatus(tr("Notifications for Kiebitz are off: System Settings › Notifications"),
                       8000);
        });
    }
    // The openings part's filter on the Library goes with it.
    if (m_trainingFilter && part != training::Part::Openings) {
        m_trainingFilter = false;
        m_libraryView->showAllTypes();
    }
    switch (part) {
    case training::Part::Openings: {
        // The Library with your repertoire files (one side's, or all), in their folders: you
        // pick the one to practise.
        std::optional<core::Color> side;
        if (TrainingSession::openingsSide() == TrainingSession::Side::White) {
            side = core::Color::White;
        } else if (TrainingSession::openingsSide() == TrainingSession::Side::Black) {
            side = core::Color::Black;
        }
        m_libraryView->showRepertoires(side);
        m_trainingFilter = true;
        showSection(Section::Library);
        break;
    }
    case training::Part::Middlegame:
        m_puzzlesView->train(QStringLiteral("middlegame"));
        showSection(Section::Puzzles);
        break;
    case training::Part::Tactics:
        m_puzzlesView->train({});
        showSection(Section::Puzzles);
        break;
    case training::Part::Endgames:
        m_puzzlesView->train(QStringLiteral("endgame"));
        showSection(Section::Puzzles);
        break;
    case training::Part::SelfStudy:
        showSection(Section::Training); // the timer runs; the board's practice, if any, ends
        break;
    }
}

void MainWindow::onPracticeStarted(core::Color side) {
    // Your side at the bottom; the moves (they'd give the answers away) and the engine off.
    m_boardPage->setFlipped(side == core::Color::Black);
    m_enginePanel->setPosition(std::nullopt);
    // No arrows left from the game: the continuations at a branch point and the engine's lines
    // would show the answers (and stay on positions they don't belong to).
    m_board->setCandidates({}, -1);
    m_boardPage->setAnalysis({}, std::nullopt, false);
    m_shortcuts->setGameKeysEnabled(false);
    m_boardPage->showPractice(true);
}

void MainWindow::onPracticeStopped() {
    m_boardPage->showPractice(false);
    m_shortcuts->setGameKeysEnabled(doc() != nullptr);
    if (doc() != nullptr) {
        showDocument(); // the game as it was, from its own side
    }
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
    m_moves->commentBox()->setFocus();
    m_moves->commentBox()->moveCursor(QTextCursor::End);
}

void MainWindow::onCommentEdited(const QString& text) {
    GameDocument* d = doc();
    if (d == nullptr) {
        return;
    }
    // The words typed, with the comment's commands ([%clk ...]) kept as they were.
    d->game.setComment(d->current,
                       core::withCommentText(d->game.node(d->current).comment, text.toStdString()));
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
    m_documents->updateTitles();
    if (name == u"Result") {
        refreshNotation();
    }
    if (name == u"Orientation") {
        orientBoard(*d); // a repertoire's side: the board turns with it
    }
}

} // namespace kiebitz::app
