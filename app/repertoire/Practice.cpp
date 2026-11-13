#include "Practice.hpp"

#include "board/BoardView.hpp"
#include "kiebitz/core/annotations.hpp"
#include "kiebitz/repertoire/drill.hpp"
#include "repertoire/PracticeStats.hpp"
#include "repertoire/RepertoireStore.hpp"
#include "sound/Sounds.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"
#include "style/Layout.hpp"
#include "widgets/Card.hpp"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace kiebitz::app {
namespace {

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

} // namespace

Practice::Practice(PracticeStats& stats, BoardView& board, Sounds& sounds, AppStyle& style,
                   std::function<const GameDocument*()> shown, QObject* parent)
    : QObject(parent), m_stats(stats), m_board(board), m_sounds(sounds), m_style(style),
      m_shown(std::move(shown)) {
    buildCard();
}

Practice::~Practice() = default;

void Practice::buildCard() {
    m_card = ui::card();
    m_card->setObjectName(QStringLiteral("practiceCard"));
    auto* layout = new QVBoxLayout(m_card);
    layout->setContentsMargins(layout::kCard);
    layout->setSpacing(layout::kCardSpacing);
    m_title = ui::label(QString(), "cardTitle");
    layout->addWidget(m_title);
    // The status with a tick or a cross, the file's explanation as a quote, then the progress.
    m_mark = new QLabel;
    m_mark->setFixedSize(18, 18);
    m_status = ui::label(QString(), "practiceStatus");
    m_status->setObjectName(QStringLiteral("practiceStatus"));
    m_status->setWordWrap(true);
    auto* statusRow = new QHBoxLayout;
    statusRow->setSpacing(8);
    statusRow->addWidget(m_mark, 0, Qt::AlignTop);
    statusRow->addWidget(m_status, 1);
    layout->addLayout(statusRow);
    m_why = ui::label(QString(), "quote");
    m_why->setObjectName(QStringLiteral("practiceWhy"));
    m_why->setWordWrap(true);
    m_why->hide();
    layout->addWidget(m_why);
    layout->addSpacing(4);
    m_bar = new QProgressBar;
    m_bar->setObjectName(QStringLiteral("practiceBar"));
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(4);
    layout->addWidget(m_bar);
    m_progress = ui::label(QString(), "engineStatus");
    m_progress->setObjectName(QStringLiteral("practiceProgress"));
    layout->addWidget(m_progress);
    m_summary = ui::label(QString(), "pvLine");
    m_summary->setObjectName(QStringLiteral("practiceSummary"));
    m_summary->setWordWrap(true);
    m_summary->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(m_summary, 1);
    auto* buttons = new QHBoxLayout;
    m_again = new QPushButton(tr("Again"));
    m_again->setObjectName(QStringLiteral("practiceAgain"));
    m_again->setProperty("role", "primary");
    m_again->setFocusPolicy(Qt::NoFocus);
    connect(m_again, &QPushButton::clicked, this, &Practice::againRequested);
    auto* stop = new QPushButton(tr("Stop"));
    stop->setObjectName(QStringLiteral("practiceStop"));
    stop->setProperty("role", "secondary");
    stop->setFocusPolicy(Qt::NoFocus);
    connect(stop, &QPushButton::clicked, this, &Practice::stop);
    buttons->addWidget(m_again);
    buttons->addWidget(stop);
    buttons->addStretch();
    layout->addLayout(buttons);
}

const GameDocument* Practice::document() const {
    const GameDocument* shown = m_shown();
    return shown != nullptr && shown->id == m_document ? shown : nullptr;
}

void Practice::setStatus(const QString& text, int mood) {
    m_status->setText(text);
    const char* name = "";
    if (mood != 0) {
        name = mood > 0 ? "good" : "bad";
    }
    if (m_status->property("mood").toString() != QLatin1String(name)) {
        m_status->setProperty("mood", QString::fromLatin1(name));
        m_status->style()->unpolish(m_status);
        m_status->style()->polish(m_status);
    }
    const auto& c = m_style.colors();
    if (mood == 0) {
        m_mark->clear();
    } else {
        const IconKind icon = mood > 0 ? IconKind::Check : IconKind::Close;
        m_mark->setPixmap(makeIcon(icon, mood > 0 ? c.good : c.bad, c.dimmed, 18).pixmap(18, 18));
    }
}

void Practice::setWhy(const QString& text) {
    m_why->setText(text);
    m_why->setVisible(!text.isEmpty());
}

void Practice::start(const GameDocument& document, core::Color side, NodeId fileStart,
                     bool fromHere, int depth, bool mistakes) {
    const QString nothing = tr("No mistakes to practise in this file.");
    // Mistakes: only the lines through positions you still get wrong (from the start, any depth).
    const std::unordered_set<std::uint64_t>* focus = mistakes ? m_stats.mistakes(side) : nullptr;
    if (mistakes && (focus == nullptr || focus->empty())) {
        emit message(nothing);
        return;
    }
    if (!m_drill || m_document != document.id || mistakes) {
        // From where the file starts (its flag), unless "From Here" says otherwise.
        m_start = fileStart == core::kNoNode ? Game::root() : fileStart;
        if (fromHere && !mistakes) {
            m_start = document.current;
        }
    }
    const bool wasActive = active();
    m_fromHere = fromHere && !mistakes;
    m_depth = mistakes ? 0 : depth;
    m_mistakes = mistakes;
    m_document = document.id;
    ++m_run;
    m_drill = std::make_unique<repertoire::Drill>(
        document.game, side, m_start, m_depth,
        static_cast<std::uint32_t>(QDateTime::currentMSecsSinceEpoch()), focus);
    if (m_drill->lines() == 0) { // the mistakes aren't in this file
        m_drill.reset();
        emit message(nothing);
        if (wasActive) {
            emit stopped(); // the practice that was running is gone too
        }
        return;
    }
    emit started(side);
    m_title->setText((mistakes ? tr("PRACTICE MISTAKES") : tr("PRACTICE")) +
                     QStringLiteral("  ·  ") + document.title());
    m_summary->clear();
    m_again->hide();
    setStatus({}, 0);
    setWhy({});
    showNode(m_start, false);
    updateProgress();
    step();
}

void Practice::stop() {
    if (!m_drill) {
        return;
    }
    m_drill.reset();
    ++m_run;
    emit stopped();
}

void Practice::showNode(NodeId id, bool animate, std::vector<core::Arrow> arrows) {
    const GameDocument* d = document();
    if (d == nullptr) {
        return;
    }
    const auto lastMove = id == Game::root() ? std::nullopt : std::optional(d->game.node(id).move);
    m_board.setPosition(d->game.positionAt(id), lastMove, animate);
    if (animate) {
        m_sounds.playMove(d->game, id);
    }
    m_board.setDrawings(std::move(arrows), {}); // the file's own arrows would give answers away
}

void Practice::step() {
    if (!m_drill) {
        return;
    }
    const int run = m_run;
    const auto later = [this, run](int ms, auto then) {
        QTimer::singleShot(ms, this, [this, run, then] {
            if (m_drill && run == m_run) { // not stopped or restarted meanwhile
                then();
            }
        });
    };
    if (m_drill->lineEnded()) {
        m_drill->nextLine();
        updateProgress();
        if (m_drill->finished()) {
            const GameDocument* d = document();
            if (d != nullptr && d->file) {
                m_stats.load(RepertoireStore::sideOf(*d->file)); // its mistakes changed
            }
            // The end: what went wrong, to look at again.
            QStringList missed;
            for (const NodeId node : m_drill->missed()) {
                if (d == nullptr) {
                    break;
                }
                missed << moveLabel(d->game, d->game.mainChild(node));
            }
            setStatus(m_drill->mistakes() == 0 ? tr("Done: no mistakes") : tr("Done"), 1);
            setWhy({});
            m_summary->setText(
                missed.isEmpty()
                    ? QString()
                    : tr("To look at again: %1").arg(missed.join(QStringLiteral(", "))));
            m_again->show();
            return;
        }
        setStatus(tr("Line done · next one"), 0);
        later(700, [this] {
            setWhy({}); // the last explanation belongs to the old line
            showNode(m_drill->current(), false);
            step();
        });
        return;
    }
    if (m_drill->yourMove()) {
        later(250, [this] {
            if (m_status->property("mood").toString() != u"bad") {
                setStatus(tr("Your move"), 0);
            }
        });
        return;
    }
    later(450, [this] { // the opponent replies after a moment, so the move can be seen
        if (const auto reply = m_drill->opponentMove()) {
            showNode(*reply, true);
            step();
        }
    });
}

void Practice::answer(core::Move move) {
    const GameDocument* d = document();
    if (!m_drill || d == nullptr) {
        return;
    }
    if (!m_drill->yourMove()) {
        showNode(m_drill->current(), false); // not your turn: take it back
        return;
    }
    const repertoire::Drill::Answer answer = m_drill->play(move);
    if (answer.first && d->file) { // the first answer at a position counts
        m_stats.record(RepertoireStore::sideOf(*d->file), answer.position, answer.correct);
    }
    if (answer.correct) {
        setStatus(tr("Correct"), 1);
        showNode(m_drill->current(), true);
        step();
    } else {
        const NodeId expected = d->game.mainChild(m_drill->current());
        setStatus(tr("Not quite. The repertoire plays %1").arg(moveLabel(d->game, expected)), -1);
        // Why: the comments on the repertoire's move. They stay until the next mistake or
        // line, so there is time to read them.
        setWhy(explanation(d->game, expected));
        showNode(m_drill->current(), false,
                 {{answer.expected.from, answer.expected.to, core::MarkColor::Green}});
    }
    updateProgress();
}

void Practice::updateProgress() {
    if (!m_drill) {
        return;
    }
    const int mistakes = m_drill->mistakes();
    m_bar->setRange(0, static_cast<int>(std::max<std::size_t>(m_drill->lines(), 1)));
    m_bar->setValue(static_cast<int>(m_drill->linesDone()));
    m_progress->setText(
        tr("Line %1 of %2  ·  %3 correct  ·  %4")
            .arg(std::min(m_drill->linesDone() + 1, m_drill->lines()))
            .arg(m_drill->lines())
            .arg(m_drill->correct())
            .arg(mistakes == 1 ? tr("1 mistake") : tr("%1 mistakes").arg(mistakes)));
}

} // namespace kiebitz::app
