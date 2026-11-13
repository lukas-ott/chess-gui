#include "ActivityGrid.hpp"

#include "TrainingHistory.hpp"
#include "TrainingSession.hpp"
#include "style/AppStyle.hpp"

#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <array>
#include <numeric>
#include <utility>

namespace kiebitz::app {
namespace {

constexpr int kGap = 3;
constexpr int kMinCell = 6;
constexpr int kMaxCell = 14;
constexpr int kColumns = 54; ///< room for any view: a calendar year can touch 54 weeks
constexpr int kLabels = 30;  ///< the weekday names on the left
constexpr int kMonths = 16;  ///< the month names on top
constexpr int kLegend = 22;  ///< "Less ▢▢▢▢▢ More" below

int minutesOf(const training::DaySeconds& seconds) {
    return std::accumulate(seconds.begin(), seconds.end(), 0) / 60;
}

} // namespace

ActivityGrid::ActivityGrid(const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_style(style) {
    setObjectName(QStringLiteral("activityGrid"));
    setMouseTracking(true);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void ActivityGrid::setDays(std::map<training::Day, training::DaySeconds> days,
                           training::Day today) {
    m_days = std::move(days);
    m_today = today;
    update();
}

void ActivityGrid::setYear(std::optional<int> year) {
    m_year = year;
    update();
}

int ActivityGrid::cellFor(int width) {
    return std::clamp(((width - kLabels) / kColumns) - kGap, kMinCell, kMaxCell);
}

int ActivityGrid::heightForWidth(int width) const {
    const int step = cellFor(width) + kGap;
    return kMonths + (7 * step) + kLegend;
}

QSize ActivityGrid::sizeHint() const {
    const int width = kLabels + (kColumns * (10 + kGap));
    return {width, heightForWidth(width)};
}

QSize ActivityGrid::minimumSizeHint() const {
    const int width = kLabels + (kColumns * (kMinCell + kGap));
    return {width, heightForWidth(width)};
}

int ActivityGrid::rowOf(training::Day day) {
    const int first = static_cast<int>(QLocale().firstDayOfWeek()); // Monday = 1
    return (TrainingHistory::dateOf(day).dayOfWeek() - first + 7) % 7;
}

training::Day ActivityGrid::firstDay() const {
    if (m_year) {
        return TrainingHistory::dayOf(QDate(*m_year, 1, 1));
    }
    return m_today - 364;
}

training::Day ActivityGrid::lastDay() const {
    if (m_year) {
        return std::min(m_today, TrainingHistory::dayOf(QDate(*m_year, 12, 31)));
    }
    return m_today;
}

training::Day ActivityGrid::firstColumnDay() const {
    return firstDay() - rowOf(firstDay());
}

int ActivityGrid::weeks() const {
    // Through the end of the range (for a year: its last week, even the days still to come).
    const training::Day end = m_year ? TrainingHistory::dayOf(QDate(*m_year, 12, 31)) : m_today;
    return ((end - firstColumnDay()) / 7) + 1;
}

QRect ActivityGrid::cellOf(training::Day day) const {
    if (day < firstDay() || day > lastDay()) {
        return {};
    }
    const int step = cell() + kGap;
    const int column = (day - firstColumnDay()) / 7;
    return {kLabels + (column * step), kMonths + (rowOf(day) * step), cell(), cell()};
}

int ActivityGrid::levelOf(training::Day day) const {
    const auto found = m_days.find(day);
    const int minutes = found != m_days.end() ? minutesOf(found->second) : 0;
    if (minutes <= 0) {
        return 0;
    }
    if (minutes < 15) {
        return 1;
    }
    if (minutes < 30) {
        return 2;
    }
    return minutes < 60 ? 3 : 4;
}

QColor ActivityGrid::shade(int level) const {
    const auto& c = m_style.colors();
    if (level <= 0) {
        return c.raised;
    }
    // The accent, from faint to full, over the card.
    const double amount = std::array{0.3, 0.5, 0.75, 1.0}[static_cast<std::size_t>(level - 1)];
    const auto mix = [amount](int from, int to) {
        return static_cast<int>(from + ((to - from) * amount));
    };
    return {mix(c.surface.red(), c.accent.red()), mix(c.surface.green(), c.accent.green()),
            mix(c.surface.blue(), c.accent.blue())};
}

void ActivityGrid::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto& c = m_style.colors();
    QFont small = font();
    small.setPixelSize(10);
    painter.setFont(small);
    const QLocale locale;
    const int size = cell();
    const int step = size + kGap;
    const qreal radius = size / 4.5;

    // The weekday names on rows 1, 3 and 5, as GitHub does.
    const int first = static_cast<int>(locale.firstDayOfWeek());
    painter.setPen(c.muted);
    for (const int row : {1, 3, 5}) {
        const int weekday = ((first - 1 + row) % 7) + 1;
        painter.drawText(QRect(0, kMonths + (row * step), kLabels - 4, size),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         locale.dayName(weekday, QLocale::ShortFormat));
    }

    // A month's name over the column its first day is in (if there's room since the last).
    int lastLabel = -10;
    for (training::Day day = firstDay(); day <= lastDay(); ++day) {
        const QDate date = TrainingHistory::dateOf(day);
        if (date.day() != 1 && day != firstDay()) {
            continue;
        }
        const int column = (day - firstColumnDay()) / 7;
        if (column - lastLabel < 3) {
            continue;
        }
        painter.drawText(QRect(kLabels + (column * step), 0, 4 * step, kMonths - 3),
                         Qt::AlignLeft | Qt::AlignBottom,
                         locale.monthName(date.month(), QLocale::ShortFormat));
        lastLabel = column;
    }

    for (training::Day day = firstDay(); day <= lastDay(); ++day) {
        const QRect square = cellOf(day);
        painter.setPen(Qt::NoPen);
        painter.setBrush(shade(levelOf(day)));
        painter.drawRoundedRect(square, radius, radius);
        if (day == m_today) { // today: outlined
            painter.setPen(QPen(c.muted, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(square).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        }
    }

    // Less ▢▢▢▢▢ More, under the right end of the columns.
    const int right = kLabels + (weeks() * step) - kGap;
    const int y = kMonths + (7 * step) + 6;
    const QString less = tr("Less");
    const QString more = tr("More");
    const QFontMetrics metrics(small);
    const int key = std::min(size, 12);
    int x = right - metrics.horizontalAdvance(more);
    painter.setPen(c.muted);
    painter.drawText(QRect(x, y - 1, metrics.horizontalAdvance(more), key + 2), Qt::AlignVCenter,
                     more);
    x -= 4 + (5 * (key + kGap));
    for (int level = 0; level <= 4; ++level) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(shade(level));
        painter.drawRoundedRect(QRect(x + (level * (key + kGap)), y, key, key), radius, radius);
    }
    x -= 4 + metrics.horizontalAdvance(less);
    painter.setPen(c.muted);
    painter.drawText(QRect(x, y - 1, metrics.horizontalAdvance(less), key + 2), Qt::AlignVCenter,
                     less);
}

void ActivityGrid::mouseMoveEvent(QMouseEvent* event) {
    const QPoint at = event->position().toPoint();
    for (training::Day day = firstDay(); day <= lastDay(); ++day) {
        if (!cellOf(day).contains(at)) {
            continue;
        }
        // "Tue 12 Nov 2026: 45 min · Openings 15, Tactics 20, Self-study 10"
        QString text =
            QLocale().toString(TrainingHistory::dateOf(day), QStringLiteral("ddd d MMM yyyy"));
        const auto found = m_days.find(day);
        const int minutes = found != m_days.end() ? minutesOf(found->second) : 0;
        if (minutes <= 0) {
            text += QStringLiteral(": ") + tr("no training");
        } else {
            text += QStringLiteral(": ") + tr("%1 min").arg(minutes);
            QStringList parts;
            for (std::size_t k = 0; k < training::kParts; ++k) {
                if (const int part = found->second[k] / 60; part > 0) {
                    parts << QStringLiteral("%1 %2").arg(
                        TrainingSession::name(training::kAllParts[k]), QString::number(part));
                }
            }
            if (!parts.isEmpty()) {
                text += QStringLiteral("  ·  ") + parts.join(QStringLiteral(", "));
            }
        }
        QToolTip::showText(event->globalPosition().toPoint(), text, this, cellOf(day));
        return;
    }
    QToolTip::hideText();
}

} // namespace kiebitz::app
