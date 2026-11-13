#include "MovesCard.hpp"

#include "notation/NotationView.hpp"
#include "settings/Settings.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"
#include "style/Layout.hpp"
#include "widgets/Card.hpp"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <tuple>
#include <utility>

namespace kiebitz::app {
namespace {

/// "12… Nf6": a move as the move list numbers it.
QString numbered(const core::Game& game, core::NodeId id) {
    const bool white = game.mover(id) == core::Color::White;
    return QStringLiteral("%1%2 %3")
        .arg(game.moveNumber(id))
        .arg(white ? QStringLiteral(".") : QStringLiteral("…"))
        .arg(QString::fromStdString(game.node(id).san));
}

} // namespace

MovesCard::MovesCard(AppStyle& style, QWidget* parent) : QFrame(parent), m_style(style) {
    setProperty("card", true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(layout::kCard);
    layout->setSpacing(layout::kCardSpacing);
    m_title = ui::label(tr("MOVES"), "cardTitle");
    m_title->setObjectName(QStringLiteral("movesTitle"));
    // The title, and how to show the moves: flowing text or rows (remembered).
    auto* titleRow = new QHBoxLayout;
    titleRow->addWidget(m_title, 1);
    auto* views = new QWidget;
    views->setObjectName(QStringLiteral("notationViews"));
    views->setProperty("role", "segmented");
    auto* viewsLayout = new QHBoxLayout(views);
    viewsLayout->setContentsMargins(2, 2, 2, 2);
    viewsLayout->setSpacing(2);
    m_views = new QButtonGroup(views);
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
        m_views->addButton(segment, id);
        viewsLayout->addWidget(segment);
    }
    titleRow->addWidget(views);
    layout->addLayout(titleRow);

    m_notation = new NotationView(m_style);
    m_notation->setMinimumHeight(200); // always room for a good few lines of moves
    m_notation->setRows(rows);
    connect(m_views, &QButtonGroup::idClicked, this,
            [](int id) { Settings::instance().setValue(Settings::kNotationRows, id == 1); });
    layout->addWidget(m_notation, 1);

    // The current move's comment, edited in place, under a header naming the move.
    m_commentTitle = ui::label(QString(), "cardTitle");
    layout->addWidget(m_commentTitle);
    m_commentBox = new QPlainTextEdit;
    m_commentBox->setObjectName(QStringLiteral("commentBox"));
    m_commentBox->setTabChangesFocus(true);
    m_commentBox->setFixedHeight(58);
    connect(m_commentBox, &QPlainTextEdit::textChanged, this,
            [this] { emit commentEdited(m_commentBox->toPlainText()); });
    layout->addWidget(m_commentBox);

    // At a branch point: the continuations to choose from (↑ ↓ to choose, → to play).
    m_forkPanel = new QWidget;
    m_forkLayout = new QVBoxLayout(m_forkPanel);
    m_forkLayout->setContentsMargins(0, 0, 0, 0);
    m_forkLayout->setSpacing(4);
    m_forkPanel->hide();
    layout->addWidget(m_forkPanel);

    // Start / back / forward / end; each mirrors a Game menu action. Icons: restyle().
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    const auto addButton = [&](const QString& tip, void (MovesCard::*signal)()) {
        auto* button = new QPushButton;
        button->setIconSize(QSize(16, 16));
        button->setProperty("role", "nav");
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this, signal);
        buttons->addWidget(button);
        return button;
    };
    m_startButton = addButton(tr("Start (Home)"), &MovesCard::startRequested);
    m_backButton = addButton(tr("Back (←)"), &MovesCard::backRequested);
    m_forwardButton = addButton(tr("Forward (→)"), &MovesCard::forwardRequested);
    m_endButton = addButton(tr("End (End)"), &MovesCard::endRequested);
    layout->addLayout(buttons);
    restyle();
}

void MovesCard::setTitle(const QString& title) {
    m_title->setText(title);
}

void MovesCard::setRows(bool rows) {
    m_notation->setRows(rows);
    if (auto* button = m_views->button(rows ? 1 : 0)) {
        button->setChecked(true);
    }
}

void MovesCard::showComment(const core::Game& game, core::NodeId id) {
    {
        const QSignalBlocker block(m_commentBox);
        m_commentBox->setPlainText(QString::fromStdString(game.node(id).comment));
        m_commentBox->setPlaceholderText(id == core::Game::root()
                                             ? tr("Add a comment about the game…")
                                             : tr("Add a comment about this move…"));
    }
    // "COMMENT · 12… Nf6": which move the box belongs to.
    const QString move = id == core::Game::root() ? tr("GAME") : numbered(game, id);
    m_commentTitle->setText(tr("COMMENT") + QStringLiteral("  ·  ") + move);
}

void MovesCard::showContinuations(const core::Game* game, core::NodeId at, int choice, bool hint) {
    // Rebuild the list (it's at most a handful of buttons).
    while (const auto* item = m_forkLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto children = game != nullptr ? game->children(at) : std::vector<core::NodeId>{};
    if (game == nullptr || children.size() < 2) {
        m_forkPanel->hide();
        return;
    }
    if (hint) {
        m_forkLayout->addWidget(
            ui::label(tr("↑ ↓ to choose a continuation, → to play it"), "hint"));
    }
    for (std::size_t i = 0; i < children.size(); ++i) {
        const core::NodeId child = children[i];
        auto* button = new QPushButton(numbered(*game, child));
        button->setProperty("role", "fork");
        button->setProperty("chosen", std::cmp_equal(i, choice));
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this,
                [this, child] { emit continuationClicked(child); });
        m_forkLayout->addWidget(button);
    }
    m_forkPanel->show();
}

void MovesCard::setNavigation(bool atStart, bool atEnd) {
    m_backButton->setEnabled(!atStart);
    m_startButton->setEnabled(!atStart);
    m_forwardButton->setEnabled(!atEnd);
    m_endButton->setEnabled(!atEnd);
}

void MovesCard::restyle() {
    const auto& c = m_style.colors();
    m_startButton->setIcon(makeIcon(IconKind::First, c.text, c.dimmed));
    m_backButton->setIcon(makeIcon(IconKind::Previous, c.text, c.dimmed));
    m_forwardButton->setIcon(makeIcon(IconKind::Next, c.text, c.dimmed));
    m_endButton->setIcon(makeIcon(IconKind::Last, c.text, c.dimmed));
}

} // namespace kiebitz::app
