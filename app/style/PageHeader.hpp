#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;

namespace kiebitz::app {

/// The top line of a page: its title, an optional muted note next to it ("162 files · 162
/// games") and actions on the right. Every page uses it, so titles sit in the same place and the
/// same size everywhere.
class PageHeader : public QWidget {
    Q_OBJECT

public:
    explicit PageHeader(const QString& title, QWidget* parent = nullptr);

    void setSubtitle(const QString& text);
    void setSubtitleToolTip(const QString& text);
    /// Adds a control on the right (in order, left to right).
    void addAction(QWidget* widget);

private:
    QLabel* m_subtitle = nullptr;
    QHBoxLayout* m_layout = nullptr;
};

} // namespace kiebitz::app
