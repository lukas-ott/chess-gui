#include "BoardPage.hpp"

#include "board/BoardView.hpp"
#include "board/EvalBar.hpp"
#include "settings/Settings.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace kiebitz::app {
namespace {

constexpr int kEvalBarGap = 6; ///< between the eval bar and the board
/// Above and below the board, for the clocks; kept when there are none, so the board never moves.
constexpr int kClockRoom = 22;

} // namespace

BoardPage::BoardPage(QWidget* parent) : QWidget(parent) {
    m_board = new BoardView;
    m_board->setObjectName(QStringLiteral("gameBoard"));

    m_sideSplitter = new QSplitter(Qt::Vertical);
    m_movesStack = new QStackedWidget;
    m_sideSplitter->addWidget(m_movesStack);
    m_sideSplitter->setChildrenCollapsible(false);

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

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 4, 10, 10);
    layout->addWidget(m_mainSplitter);
    m_evalBar = new EvalBar(this); // floats over the page; see placeEvalBar()
    m_evalBar->hide();
    for (QLabel*& clock : m_clocks) {
        clock = new QLabel(this);
        clock->setProperty("role", "boardClock");
        clock->setAlignment(Qt::AlignCenter);
        clock->hide();
    }
    m_clocks[0]->setObjectName(QStringLiteral("clockTop"));
    m_clocks[1]->setObjectName(QStringLiteral("clockBottom"));
    connect(m_mainSplitter, &QSplitter::splitterMoved, this, &BoardPage::placeEvalBar);
    connect(m_sideSplitter, &QSplitter::splitterMoved, this,
            [this] { m_defaultSideSplit = false; });
}

void BoardPage::setSide(QWidget* moves, QWidget* practice, QWidget* panels) {
    m_movesStack->addWidget(moves);
    m_movesStack->addWidget(practice);
    m_sideSplitter->addWidget(panels);
    m_sideSplitter->setStretchFactor(0, 2);
    m_sideSplitter->setStretchFactor(1, 1);
}

bool BoardPage::isFlipped() const {
    return m_board->isFlipped();
}

void BoardPage::setFlipped(bool flipped) {
    m_board->setFlipped(flipped);
    m_evalBar->setFlipped(flipped);
}

void BoardPage::showPractice(bool on) {
    m_movesStack->setCurrentIndex(on ? 1 : 0);
}

void BoardPage::setAnalysis(const std::vector<core::Move>& best, std::optional<engine::Score> score,
                            bool running) {
    m_board->setEngineMoves(best);
    m_evalBar->setScore(score);
    if (running && !m_evalBar->isVisible()) {
        placeEvalBar();
        m_evalBar->raise();
    }
    m_evalBar->setVisible(running); // its space is kept: nothing else moves
}

void BoardPage::setClocks(const std::array<QString, 2>& whiteBlack) {
    // White at the bottom unless the board is flipped.
    const bool flipped = m_board->isFlipped();
    const std::array<QString, 2> shown{flipped ? whiteBlack[0] : whiteBlack[1],
                                       flipped ? whiteBlack[1] : whiteBlack[0]};
    for (std::size_t k = 0; k < 2; ++k) {
        m_clocks[k]->setText(shown[k]);
        m_clocks[k]->setVisible(!shown[k].isEmpty());
    }
    placeEvalBar();
}

bool BoardPage::eventFilter(QObject* watched, QEvent* event) {
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
            emit wheelStep(-1);
        }
        while (m_wheel <= -120) {
            m_wheel += 120;
            emit wheelStep(+1);
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void BoardPage::placeEvalBar() {
    // As tall as the board's squares, and centred in the gap between them and the moves panel.
    const QRect a8 = m_board->squareRect(core::Square::at(0, 7));
    const QRect h1 = m_board->squareRect(core::Square::at(7, 0));
    const QRect squares =
        QRect(m_board->mapTo(this, a8.topLeft()), m_board->mapTo(this, h1.bottomRight()))
            .normalized()
            .united(
                QRect(m_board->mapTo(this, h1.topLeft()), m_board->mapTo(this, a8.bottomRight()))
                    .normalized());
    const int panel = m_sideSplitter->mapTo(this, QPoint(0, 0)).x();
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

void BoardPage::fit() {
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

void BoardPage::restoreLayout(const QSettings& settings) {
    m_mainSplitter->restoreState(
        settings.value(QStringLiteral("window/mainSplitter")).toByteArray());
    if (!m_sideSplitter->restoreState(
            settings.value(QStringLiteral("window/sideSplitter")).toByteArray())) {
        m_defaultSideSplit = true; // until the user drags the divider
    }
}

void BoardPage::saveLayout(QSettings& settings) const {
    settings.setValue(QStringLiteral("window/mainSplitter"), m_mainSplitter->saveState());
    settings.setValue(QStringLiteral("window/sideSplitter"), m_sideSplitter->saveState());
}

} // namespace kiebitz::app
