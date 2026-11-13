#include "TrainingView.hpp"

#include "ActivityGrid.hpp"
#include "TrainingSession.hpp"
#include "style/AppStyle.hpp"
#include "style/Layout.hpp"
#include "style/PageHeader.hpp"
#include "widgets/Card.hpp"
#include "widgets/Stepper.hpp"
#include "widgets/Toggle.hpp"

#include <QComboBox>
#include <QDate>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <numeric>
#include <optional>
#include <set>

namespace kiebitz::app {
namespace {

using training::Part;

/// What each part does, in a few words.
QString description(Part part) {
    switch (part) {
    case Part::Openings:
        return TrainingView::tr("Pick a file to practise");
    case Part::Middlegame:
        return TrainingView::tr("Middlegame puzzles");
    case Part::Tactics:
        return TrainingView::tr("Puzzles around your rating");
    case Part::Endgames:
        return TrainingView::tr("Endgame puzzles");
    case Part::SelfStudy:
        break;
    }
    return TrainingView::tr("Your own study, timed");
}

/// "1 day", "4 days".
QString daysText(int days) {
    return days == 1 ? TrainingView::tr("1 day") : TrainingView::tr("%1 days").arg(days);
}

/// "45 min", "2 h 5 min".
QString minutesText(int minutes) {
    if (minutes < 60) {
        return TrainingView::tr("%1 min").arg(minutes);
    }
    return minutes % 60 == 0 ? TrainingView::tr("%1 h").arg(minutes / 60)
                             : TrainingView::tr("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}

void repolish(QWidget* widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

} // namespace

QString clockText(qint64 ms) {
    const qint64 seconds = (ms + 999) / 1000; // 0:01 until it's really over
    if (seconds >= 3600) {
        return QStringLiteral("%1:%2:%3")
            .arg(seconds / 3600)
            .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

TrainingView::TrainingView(TrainingSession& session, TrainingHistory& stats, const AppStyle& style,
                           QWidget* parent)
    : QWidget(parent), m_session(session), m_stats(stats), m_style(style) {
    setObjectName(QStringLiteral("trainingView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(layout::kPage);
    layout->setSpacing(layout::kPageSpacing);

    m_header = new PageHeader(tr("Training"));
    m_start = new QPushButton(tr("Start Training"));
    m_start->setObjectName(QStringLiteral("trainingStart"));
    m_start->setProperty("role", "primary");
    // As wide as its longer text, so it doesn't change size between them.
    m_start->setMinimumWidth(m_start->fontMetrics().horizontalAdvance(tr("Stop Training")) + 48);
    connect(m_start, &QPushButton::clicked, this, [this] {
        if (m_session.running()) {
            m_session.stop();
        } else {
            m_session.start();
        }
    });
    m_header->addAction(m_start);
    layout->addWidget(m_header);

    // The plan: a row per part.
    auto* plan = ui::card();
    plan->setObjectName(QStringLiteral("trainingPlan"));
    auto* planBox = new QVBoxLayout(plan);
    planBox->setContentsMargins(layout::kCard);
    planBox->setSpacing(6);
    planBox->addWidget(ui::label(tr("PLAN"), "cardTitle"));
    const std::vector<training::Step> steps = TrainingSession::plan();
    for (std::size_t k = 0; k < training::kParts; ++k) {
        const Part part = training::kAllParts[k];
        Row& row = m_rows[k];
        row.frame = new QFrame;
        row.frame->setObjectName(QStringLiteral("trainingPart"));
        row.frame->setProperty("trainingPart", true);
        auto* rowBox = new QHBoxLayout(row.frame);
        rowBox->setContentsMargins(10, 6, 10, 6);
        rowBox->setSpacing(10);
        row.on = new Toggle(m_style);
        row.on->setObjectName(QStringLiteral("trainingOn"));
        row.on->setChecked(steps[k].on);
        connect(row.on, &Toggle::toggled, this, [this] { savePlan(); });
        rowBox->addWidget(row.on);
        auto* texts = new QVBoxLayout;
        texts->setSpacing(0);
        texts->addWidget(ui::label(TrainingSession::name(part), "checkHeadline"));
        if (part == Part::Openings) {
            // Which repertoire files the Library shows.
            auto* sideRow = new QHBoxLayout;
            sideRow->setSpacing(6);
            sideRow->addWidget(ui::label(description(part), "emptyHint"));
            m_side = new QComboBox;
            m_side->setObjectName(QStringLiteral("trainingSide"));
            m_side->setFocusPolicy(Qt::NoFocus);
            m_side->addItem(tr("White repertoires"),
                            static_cast<int>(TrainingSession::Side::White));
            m_side->addItem(tr("Black repertoires"),
                            static_cast<int>(TrainingSession::Side::Black));
            m_side->addItem(tr("All repertoires"), static_cast<int>(TrainingSession::Side::All));
            m_side->setCurrentIndex(
                m_side->findData(static_cast<int>(TrainingSession::openingsSide())));
            connect(m_side, &QComboBox::activated, this, [this] {
                TrainingSession::setOpeningsSide(
                    static_cast<TrainingSession::Side>(m_side->currentData().toInt()));
            });
            texts->addLayout(sideRow);
            sideRow->addWidget(m_side);
            sideRow->addStretch();
        } else {
            texts->addWidget(ui::label(description(part), "emptyHint"));
        }
        rowBox->addLayout(texts, 1);
        row.left = ui::label(QString(), "statCaption");
        row.left->setObjectName(QStringLiteral("trainingLeft"));
        row.left->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row.left->setFixedWidth(row.left->fontMetrics().horizontalAdvance(tr("Time's up")) + 8);
        rowBox->addWidget(row.left);
        row.minutes = new Stepper;
        row.minutes->setObjectName(QStringLiteral("trainingMinutes"));
        row.minutes->setRange(5, 120);
        row.minutes->setSingleStep(5);
        row.minutes->setSuffix(tr(" min"));
        row.minutes->setValue(steps[k].minutes);
        connect(row.minutes, &Stepper::valueChanged, this, [this] { savePlan(); });
        rowBox->addWidget(row.minutes);
        planBox->addWidget(row.frame);
    }
    m_total = ui::label(QString(), "emptyHint");
    m_total->setObjectName(QStringLiteral("trainingTotal"));
    m_total->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    planBox->addWidget(m_total);
    planBox->addStretch();

    // Beside it, how you're doing: today, the streaks, the week.
    auto* progress = ui::card();
    progress->setObjectName(QStringLiteral("trainingProgress"));
    auto* progressBox = new QVBoxLayout(progress);
    progressBox->setContentsMargins(layout::kCard);
    progressBox->setSpacing(layout::kCardSpacing);
    progressBox->addWidget(ui::label(tr("PROGRESS"), "cardTitle"));
    auto* tiles = new QGridLayout;
    tiles->setSpacing(8);
    const std::array captions{tr("Today"), tr("Streak"), tr("Best streak"), tr("Last 7 days")};
    for (std::size_t i = 0; i < m_tiles.size(); ++i) {
        auto* tile = new QFrame;
        tile->setProperty("statTile", true);
        auto* tileBox = new QVBoxLayout(tile);
        tileBox->setContentsMargins(12, 10, 12, 10);
        tileBox->setSpacing(2);
        tileBox->addWidget(ui::label(captions[i], "statCaption"));
        m_tiles[i] = ui::label(QStringLiteral("–"), "statValue");
        tileBox->addWidget(m_tiles[i]);
        tiles->addWidget(tile, static_cast<int>(i / 2), static_cast<int>(i % 2));
    }
    m_tiles[0]->setObjectName(QStringLiteral("trainingToday"));
    m_tiles[1]->setObjectName(QStringLiteral("trainingStreak"));
    progressBox->addLayout(tiles);
    progressBox->addStretch();

    // Below, the whole width: the days you trained.
    auto* activity = ui::card();
    activity->setObjectName(QStringLiteral("trainingActivity"));
    auto* activityBox = new QVBoxLayout(activity);
    activityBox->setContentsMargins(layout::kCard);
    activityBox->setSpacing(layout::kCardSpacing);
    // The title, and (once you have trained in more than one year) which year the grid shows.
    auto* activityTitle = new QHBoxLayout;
    activityTitle->addWidget(ui::label(tr("ACTIVITY"), "cardTitle"), 1);
    m_year = new QComboBox;
    m_year->setObjectName(QStringLiteral("trainingYear"));
    m_year->setFocusPolicy(Qt::NoFocus);
    m_year->addItem(tr("Last 12 months"), -1);
    QSizePolicy keep = m_year->sizePolicy();
    keep.setRetainSizeWhenHidden(true); // the row keeps its height either way
    m_year->setSizePolicy(keep);
    m_year->hide();
    connect(m_year, &QComboBox::activated, this, [this] {
        const int year = m_year->currentData().toInt();
        m_grid->setYear(year < 0 ? std::nullopt : std::optional(year));
    });
    activityTitle->addWidget(m_year);
    activityBox->addLayout(activityTitle);
    m_grid = new ActivityGrid(m_style);
    activityBox->addWidget(m_grid);
    activityBox->addStretch();

    auto* top = new QHBoxLayout;
    top->setSpacing(layout::kPageSpacing);
    top->addWidget(plan, 3);
    top->addWidget(progress, 2);
    layout->addLayout(top);
    layout->addWidget(activity, 1);

    connect(&m_session, &TrainingSession::changed, this, &TrainingView::showSession);
    connect(&m_stats, &TrainingHistory::loaded, this, &TrainingView::showStats);
    savePlan(); // the total
    showSession();
}

void TrainingView::activate() {
    m_stats.load();
}

void TrainingView::savePlan() {
    std::vector<training::Step> plan;
    int total = 0;
    for (std::size_t k = 0; k < training::kParts; ++k) {
        const Row& row = m_rows[k];
        plan.push_back({training::kAllParts[k], row.minutes->value(), row.on->isChecked()});
        total += row.on->isChecked() ? row.minutes->value() : 0;
        row.minutes->setEnabled(row.on->isChecked() && !m_session.running());
    }
    TrainingSession::savePlan(plan);
    m_total->setText(tr("%1 in all").arg(minutesText(total)));
    m_start->setEnabled(m_session.running() || total > 0);
}

void TrainingView::showSession() {
    const training::Session* session = m_session.session();
    m_start->setText(session != nullptr ? tr("Stop Training") : tr("Start Training"));
    m_start->setProperty("role", session != nullptr ? "secondary" : "primary");
    repolish(m_start);
    for (std::size_t k = 0; k < training::kParts; ++k) {
        const Row& row = m_rows[k];
        const bool current = session != nullptr && session->part() == training::kAllParts[k];
        if (row.frame->property("current").toBool() != current) {
            row.frame->setProperty("current", current);
            repolish(row.frame);
        }
        QString left;
        if (current) {
            left = session->timeUp() ? tr("Time's up")
                                     : tr("%1 left").arg(clockText(session->leftMs()));
        }
        row.left->setText(left);
        row.on->setEnabled(session == nullptr);
        row.minutes->setEnabled(session == nullptr && row.on->isChecked());
    }
    if (m_side != nullptr) {
        m_side->setEnabled(session == nullptr);
    }
    m_start->setEnabled(session != nullptr || !training::Session(TrainingSession::plan()).empty());
    showStats(m_summary); // today's minutes include the time not yet in the log
}

void TrainingView::showStats(const TrainingHistory::Summary& summary) {
    m_summary = summary;
    const training::Day today = TrainingHistory::dayOf(QDate::currentDate());
    const auto secondsOn = [&](training::Day day) {
        const auto found = summary.days.find(day);
        return found == summary.days.end()
                   ? 0
                   : std::accumulate(found->second.begin(), found->second.end(), 0);
    };
    const int todayMinutes = (secondsOn(today) + m_session.unsavedSeconds()) / 60;
    int week = m_session.unsavedSeconds();
    for (training::Day day = today - 6; day <= today; ++day) {
        week += secondsOn(day);
    }
    // Trained today (also before it's in the log): the streak includes today.
    int streak = summary.streak;
    if (secondsOn(today) == 0 && m_session.unsavedSeconds() > 0) {
        ++streak;
    }
    m_tiles[0]->setText(minutesText(todayMinutes));
    m_tiles[1]->setText(daysText(streak));
    m_tiles[2]->setText(daysText(std::max(summary.bestStreak, streak)));
    m_tiles[3]->setText(minutesText(week / 60));
    int planned = 0;
    for (const training::Step& step : TrainingSession::plan()) {
        planned += step.on ? step.minutes : 0;
    }
    m_header->setSubtitle(
        tr("Today %1 of %2  ·  %3")
            .arg(minutesText(todayMinutes), minutesText(planned), tr("%1-day streak").arg(streak)));
    m_grid->setDays(summary.days, today);
    // The years trained, newest first: a choice only once there are two.
    std::set<int> years;
    for (const auto& [day, seconds] : summary.days) {
        years.insert(TrainingHistory::dateOf(day).year());
    }
    QList<int> listed;
    for (int i = 1; i < m_year->count(); ++i) {
        listed << m_year->itemData(i).toInt();
    }
    const QList<int> wanted(years.rbegin(), years.rend());
    if (listed != wanted) {
        const int chosen = m_year->currentData().toInt();
        const QSignalBlocker block(m_year);
        while (m_year->count() > 1) {
            m_year->removeItem(1);
        }
        for (const int year : wanted) {
            m_year->addItem(QString::number(year), year);
        }
        const int index = m_year->findData(chosen);
        m_year->setCurrentIndex(std::max(index, 0));
        if (index < 0) {
            m_grid->setYear(std::nullopt);
        }
    }
    m_year->setVisible(years.size() >= 2);
}

// ---- The bar in the status bar ------------------------------------------------------------------

TrainingBar::TrainingBar(TrainingSession& session, const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_session(session), m_style(style) {
    setObjectName(QStringLiteral("trainingBar"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 4, 0);
    row->setSpacing(2);
    const auto button = [&](const QString& text, const char* name) {
        auto* b = new QToolButton;
        b->setObjectName(QString::fromLatin1(name));
        b->setText(text);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setProperty("role", "statusAction");
        row->addWidget(b);
        return b;
    };
    m_part = button(QString(), "trainingBarPart");
    m_part->setToolTip(tr("Training"));
    connect(m_part, &QToolButton::clicked, this, &TrainingBar::showRequested);
    m_pause = button(tr("Pause"), "trainingBarPause");
    connect(m_pause, &QToolButton::clicked, this,
            [this] { m_session.setPaused(!m_session.paused()); });
    m_next = button(tr("Next"), "trainingBarNext");
    connect(m_next, &QToolButton::clicked, &m_session, &TrainingSession::next);
    m_stop = button(tr("Stop"), "trainingBarStop");
    connect(m_stop, &QToolButton::clicked, &m_session, &TrainingSession::stop);
    connect(&m_session, &TrainingSession::changed, this, &TrainingBar::refresh);
    refresh();
}

QString TrainingBar::text() const {
    return m_part->text();
}

void TrainingBar::refresh() {
    const training::Session* session = m_session.session();
    setVisible(session != nullptr);
    if (session == nullptr) {
        return;
    }
    QString text = TrainingSession::name(session->part()) + QStringLiteral("  ·  ");
    if (session->timeUp()) {
        text += tr("time's up");
        if (const training::Step* upcoming = session->upcoming()) {
            text +=
                QStringLiteral("  ·  ") + tr("next: %1").arg(TrainingSession::name(upcoming->part));
        }
    } else {
        text += tr("%1 left").arg(clockText(session->leftMs()));
    }
    if (m_session.paused()) {
        text += QStringLiteral("  ·  ") + tr("paused");
    }
    m_part->setText(text);
    if (m_part->property("up").toBool() != session->timeUp()) {
        m_part->setProperty("up", session->timeUp());
        repolish(m_part);
    }
    m_pause->setText(m_session.paused() ? tr("Resume") : tr("Pause"));
    m_next->setText(session->upcoming() != nullptr ? tr("Next") : tr("Finish"));
}

} // namespace kiebitz::app
