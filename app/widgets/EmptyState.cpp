#include "EmptyState.hpp"

#include "style/AppStyle.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace kiebitz::app {

EmptyState::EmptyState(IconKind icon, const QString& title, const QString& hint,
                       const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_kind(icon), m_style(style) {
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->addStretch();
    m_icon = new QLabel;
    m_icon->setProperty("role", "emptyIcon");
    m_icon->setAlignment(Qt::AlignCenter);
    m_icon->setFixedSize(64, 64);
    layout->addWidget(m_icon, 0, Qt::AlignHCenter);
    auto* titleLabel = new QLabel(title);
    titleLabel->setProperty("role", "emptyTitle");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);
    if (!hint.isEmpty()) {
        auto* hintLabel = new QLabel(hint);
        hintLabel->setProperty("role", "emptyHint");
        hintLabel->setAlignment(Qt::AlignCenter);
        hintLabel->setWordWrap(true);
        layout->addWidget(hintLabel);
    }
    layout->addStretch();
    connect(&m_style, &AppStyle::changed, this, &EmptyState::updateIcon);
    updateIcon();
}

void EmptyState::updateIcon() {
    const auto& c = m_style.colors();
    m_icon->setPixmap(makeIcon(m_kind, c.muted, c.dimmed, 28).pixmap(28, 28));
}

} // namespace kiebitz::app
