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
    m_title = new QLabel;
    m_title->setProperty("role", "emptyTitle");
    m_title->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_title);
    m_hint = new QLabel;
    m_hint->setProperty("role", "emptyHint");
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);
    layout->addStretch();
    setText(title, hint);
    connect(&m_style, &AppStyle::changed, this, &EmptyState::updateIcon);
    updateIcon();
}

void EmptyState::setText(const QString& title, const QString& hint) {
    m_title->setText(title);
    m_hint->setText(hint);
    m_hint->setVisible(!hint.isEmpty());
}

void EmptyState::updateIcon() {
    const auto& c = m_style.colors();
    m_icon->setPixmap(makeIcon(m_kind, c.muted, c.dimmed, 28).pixmap(28, 28));
}

} // namespace kiebitz::app
