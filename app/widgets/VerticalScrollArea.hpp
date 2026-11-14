#pragma once

#include <QEvent>
#include <QScrollArea>

namespace kiebitz::app {

/// A scroll area that only scrolls up and down: its content is always exactly as wide as the
/// visible part, so nothing ever moves sideways (a trackpad would otherwise scroll a content
/// wider than the area, even with the horizontal scroll bar off). Content that doesn't fit
/// has to shorten itself (ElidedLabel).
class VerticalScrollArea : public QScrollArea {
public:
    explicit VerticalScrollArea(QWidget* parent = nullptr) : QScrollArea(parent) {
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        viewport()->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == viewport() && event->type() == QEvent::Resize) {
            fitWidth();
        }
        return QScrollArea::eventFilter(watched, event);
    }

private:
    void fitWidth() {
        if (QWidget* content = widget()) {
            content->setFixedWidth(viewport()->width());
        }
    }
};

} // namespace kiebitz::app
