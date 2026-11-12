#pragma once

#include <QDate>
#include <QWidget>

#include <optional>
#include <string>

class QPushButton;

namespace kiebitz::app {

class AppStyle;

/// A range of dates for a games table, as one button ("Any date", "Mar 12, 2023 – Jan 5,
/// 2024"). Clicking it opens a picker: shortcuts (this year, last year, the last 12 months, any
/// time) and a calendar for each end, either of which may be left open.
class DateRange : public QWidget {
    Q_OBJECT

public:
    /// `name`: its own name; its button is "<name>Button" (tests find them so).
    DateRange(const QString& name, const AppStyle& style, QWidget* parent = nullptr);

    /// As PGN date prefixes ("2024.05.12"); empty: no limit.
    [[nodiscard]] std::string from() const;
    [[nodiscard]] std::string to() const;
    [[nodiscard]] std::optional<QDate> fromDate() const { return m_from; }
    [[nodiscard]] std::optional<QDate> toDate() const { return m_to; }

    /// Sets the range (nullopt: no limit at that end), as the picker does.
    void setRange(std::optional<QDate> from, std::optional<QDate> to);
    void clear() { setRange(std::nullopt, std::nullopt); }
    /// What the button says.
    [[nodiscard]] QString text() const;

signals:
    void changed();

private:
    void openPicker();

    const AppStyle& m_style;
    QPushButton* m_button = nullptr;
    std::optional<QDate> m_from;
    std::optional<QDate> m_to;
};

} // namespace kiebitz::app
