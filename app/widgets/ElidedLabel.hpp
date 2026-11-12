#pragma once

#include <QLabel>

namespace kiebitz::app {

/// A one-line label that cuts its text off with "…" when there isn't room, instead of forcing
/// its column (and with it the window) to be as wide as the text. The full text is the tooltip.
class ElidedLabel : public QLabel {
public:
    using QLabel::QLabel;

    void setFullText(const QString& text) {
        m_full = text;
        setToolTip(text);
        updateText();
    }
    [[nodiscard]] QSize minimumSizeHint() const override {
        return {fontMetrics().horizontalAdvance(QStringLiteral("…")), QLabel::sizeHint().height()};
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        updateText();
    }

private:
    void updateText() { setText(fontMetrics().elidedText(m_full, Qt::ElideRight, width())); }

    QString m_full;
};

} // namespace kiebitz::app
