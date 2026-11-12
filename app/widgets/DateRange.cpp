#include "DateRange.hpp"

#include "style/AppStyle.hpp"
#include "style/Icons.hpp"

#include <QCalendarWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QStyle>
#include <QTextCharFormat>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <utility>

namespace kiebitz::app {
namespace {

QString pgnDate(const QDate& date) {
    return date.toString(QStringLiteral("yyyy.MM.dd"));
}

QLabel* caption(const QString& text) {
    auto* label = new QLabel(text);
    label->setProperty("role", "cardTitle");
    return label;
}

/// A month of days, drawn in the app's colours: the range picked as a soft band, this
/// calendar's end of it as a round mark, today with a ring, other months' days quiet.
class DayGrid : public QCalendarWidget {
public:
    DayGrid(const AppStyle& style, const DateRange& range, bool start)
        : m_style(style), m_range(range), m_start(start) {
        const auto& c = m_style.colors();
        QPalette palette = this->palette();
        palette.setColor(QPalette::Base, c.surface);
        palette.setColor(QPalette::AlternateBase, c.surface);
        palette.setColor(QPalette::Window, c.surface);
        palette.setColor(QPalette::Text, c.text);
        setPalette(palette);
        setGridVisible(false);
        setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
        setHorizontalHeaderFormat(QCalendarWidget::SingleLetterDayNames);
        setFirstDayOfWeek(Qt::Monday);
        QTextCharFormat names; // the day names: small and quiet, on the card's colour
        names.setForeground(c.muted);
        names.setBackground(c.surface);
        names.setFontPointSize(10);
        setHeaderTextFormat(names);
        // Weekends like the other days (Qt makes them red).
        QTextCharFormat weekend;
        weekend.setForeground(c.muted);
        setWeekdayTextFormat(Qt::Saturday, weekend);
        setWeekdayTextFormat(Qt::Sunday, weekend);
        for (const auto& [button, kind] : {std::pair{"qt_calendar_prevmonth", IconKind::Previous},
                                           std::pair{"qt_calendar_nextmonth", IconKind::Next}}) {
            if (auto* arrow = findChild<QToolButton*>(QString::fromLatin1(button))) {
                arrow->setIcon(makeIcon(kind, c.muted, c.dimmed, 14));
            }
        }
        setFixedSize(236, 210);
    }

protected:
    void paintCell(QPainter* painter, const QRect& rect, QDate date) const override {
        const auto& c = m_style.colors();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->fillRect(rect, c.surface);
        const std::optional<QDate> from = m_range.fromDate();
        const std::optional<QDate> to = m_range.toDate();
        const std::optional<QDate> mine = m_start ? from : to;
        // The range as a band (open ends reach to the edge of what is shown).
        const bool inRange = (from || to) && (!from || date >= *from) && (!to || date <= *to) &&
                             date.month() == monthShown(); // each calendar: its own month
        const QRectF cell = QRectF(rect).adjusted(1, 3, -1, -3);
        if (inRange) {
            // From the middle of the first day to the middle of the last.
            QRectF strip = QRectF(rect).adjusted(0, 3, 0, -3);
            if (from && date == *from) {
                strip.setLeft(strip.center().x());
            }
            if (to && date == *to) {
                strip.setRight(QRectF(rect).center().x());
            }
            QColor band = c.accent;
            band.setAlphaF(0.18F);
            painter->fillRect(strip, band);
        }
        const bool picked = mine && date == *mine;
        if (picked) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(c.accent);
            const double side = std::min(cell.width(), cell.height());
            painter->drawRoundedRect(QRectF(cell.center().x() - (side / 2), cell.top(), side, side),
                                     7, 7);
        } else if (date == QDate::currentDate()) {
            painter->setPen(QPen(c.accent, 1));
            painter->setBrush(Qt::NoBrush);
            const double side = std::min(cell.width(), cell.height()) - 1;
            painter->drawRoundedRect(
                QRectF(cell.center().x() - (side / 2), cell.top() + 0.5, side, side), 7, 7);
        }
        QFont font = painter->font();
        font.setPixelSize(12);
        font.setWeight(picked ? QFont::DemiBold : QFont::Normal);
        painter->setFont(font);
        QColor text = c.text;
        if (picked) {
            text = c.accentText;
        } else if (date.month() != monthShown() || date > maximumDate()) {
            text = c.dimmed; // the month before or after
        }
        painter->setPen(text);
        painter->drawText(rect, Qt::AlignCenter, QString::number(date.day()));
        painter->restore();
    }

private:
    const AppStyle& m_style;
    const DateRange& m_range;
    bool m_start; ///< the From calendar (else To)
};

} // namespace

DateRange::DateRange(const QString& name, const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_style(style) {
    setObjectName(name);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    m_button = new QPushButton;
    m_button->setObjectName(name + QStringLiteral("Button"));
    m_button->setProperty("role", "secondary");
    m_button->setFocusPolicy(Qt::NoFocus);
    const auto& c = m_style.colors();
    m_button->setIcon(makeIcon(IconKind::Calendar, c.muted, c.dimmed, 14));
    m_button->setToolTip(tr("Only the games played in these dates"));
    connect(m_button, &QPushButton::clicked, this, &DateRange::openPicker);
    row->addWidget(m_button);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); // never stretched across a row
    m_button->setText(text());
}

std::string DateRange::from() const {
    return m_from ? pgnDate(*m_from).toStdString() : std::string();
}

std::string DateRange::to() const {
    return m_to ? pgnDate(*m_to).toStdString() : std::string();
}

QString DateRange::text() const {
    const QLocale locale;
    const auto show = [&](const QDate& date) {
        return locale.toString(date, QLocale::ShortFormat);
    };
    // A whole year reads as the year.
    if (m_from && m_to && m_from->year() == m_to->year() &&
        *m_from == QDate(m_from->year(), 1, 1) && *m_to == QDate(m_to->year(), 12, 31)) {
        return QString::number(m_from->year());
    }
    if (m_from && m_to) {
        return QStringLiteral("%1 – %2").arg(show(*m_from), show(*m_to));
    }
    if (m_from) {
        return tr("Since %1").arg(show(*m_from));
    }
    if (m_to) {
        return tr("Until %1").arg(show(*m_to));
    }
    return tr("Any date");
}

void DateRange::setRange(std::optional<QDate> from, std::optional<QDate> to) {
    if (from && to && *to < *from) {
        std::swap(from, to); // the earlier one first, whichever was picked first
    }
    if (from == m_from && to == m_to) {
        return;
    }
    m_from = from;
    m_to = to;
    m_button->setText(text());
    m_button->setProperty("active", m_from.has_value() || m_to.has_value());
    m_button->style()->unpolish(m_button);
    m_button->style()->polish(m_button);
    emit changed();
}

void DateRange::openPicker() {
    // A card that floats under the button and goes away with a click elsewhere.
    auto* popup = new QFrame(this, Qt::Popup);
    popup->setObjectName(QStringLiteral("dateRangePicker"));
    popup->setAttribute(Qt::WA_TranslucentBackground);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    auto* outer = new QVBoxLayout(popup);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* body = new QFrame;
    body->setProperty("card", true);
    outer->addWidget(body);
    auto* box = new QVBoxLayout(body);
    box->setContentsMargins(16, 14, 16, 14);
    box->setSpacing(10);

    // Shortcuts.
    const QDate today = QDate::currentDate();
    auto* presets = new QHBoxLayout;
    presets->setSpacing(4);
    const std::array<std::tuple<QString, std::optional<QDate>, std::optional<QDate>>, 4> choices{{
        {tr("This year"), QDate(today.year(), 1, 1), QDate(today.year(), 12, 31)},
        {tr("Last year"), QDate(today.year() - 1, 1, 1), QDate(today.year() - 1, 12, 31)},
        {tr("Last 12 months"), today.addYears(-1).addDays(1), today},
        {tr("Any time"), std::nullopt, std::nullopt},
    }};
    for (const auto& [text, from, to] : choices) {
        auto* chip = new QPushButton(text);
        chip->setProperty("role", "chip");
        chip->setFocusPolicy(Qt::NoFocus);
        chip->setCheckable(true);
        chip->setChecked(m_from == from && m_to == to);
        connect(chip, &QPushButton::clicked, popup, [this, popup, from, to] {
            setRange(from, to);
            popup->close();
        });
        presets->addWidget(chip);
    }
    presets->addStretch();
    box->addLayout(presets);

    // A calendar for each end, and a way to leave it open.
    auto* ends = new QHBoxLayout;
    ends->setSpacing(16);
    for (const bool start : {true, false}) {
        auto* column = new QVBoxLayout;
        column->setSpacing(6);
        auto* head = new QHBoxLayout;
        head->addWidget(caption(start ? tr("FROM") : tr("TO")));
        head->addStretch();
        auto* open = new QPushButton(start ? tr("No start") : tr("No end"));
        open->setProperty("role", "link");
        open->setProperty("compact", true);
        open->setFocusPolicy(Qt::NoFocus);
        head->addWidget(open);
        column->addLayout(head);
        auto* calendar = new DayGrid(m_style, *this, start);
        calendar->setObjectName(start ? QStringLiteral("dateRangeFrom")
                                      : QStringLiteral("dateRangeTo"));
        calendar->setMaximumDate(QDate(today.year() + 1, 12, 31));
        const std::optional<QDate> shown = start ? m_from : m_to;
        calendar->setSelectedDate(shown.value_or(start ? QDate(today.year(), 1, 1) : today));
        // Both calendars show the range: repainted when it changes.
        connect(this, &DateRange::changed, calendar, [calendar] { calendar->update(); });
        connect(calendar, &QCalendarWidget::clicked, this, [this, start](const QDate& date) {
            if (start) {
                setRange(date, m_to);
            } else {
                setRange(m_from, date);
            }
        });
        connect(open, &QPushButton::clicked, this, [this, start] {
            if (start) {
                setRange(std::nullopt, m_to);
            } else {
                setRange(m_from, std::nullopt);
            }
        });
        column->addWidget(calendar);
        ends->addLayout(column);
    }
    box->addLayout(ends);

    auto* footer = new QHBoxLayout;
    auto* summary = new QLabel(text());
    summary->setProperty("role", "emptyHint");
    connect(this, &DateRange::changed, summary, [this, summary] { summary->setText(text()); });
    footer->addWidget(summary, 1);
    auto* done = new QPushButton(tr("Done"));
    done->setProperty("role", "primary");
    done->setProperty("compact", true);
    connect(done, &QPushButton::clicked, popup, &QWidget::close);
    footer->addWidget(done);
    box->addLayout(footer);

    popup->adjustSize();
    // Under the button, right-aligned with it, and kept on the screen.
    QPoint at =
        m_button->mapToGlobal(QPoint(m_button->width() - popup->width(), m_button->height() + 4));
    at.setX(std::max(at.x(), 8));
    popup->move(at);
    popup->show();
}

} // namespace kiebitz::app
