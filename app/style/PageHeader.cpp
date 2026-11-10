#include "PageHeader.hpp"

#include "Layout.hpp"

#include <QHBoxLayout>
#include <QLabel>

namespace kiebitz::app {

PageHeader::PageHeader(const QString& title, QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("pageHeader"));
    // One height for every page header, whatever controls it holds.
    setFixedHeight(layout::kControlHeight + 4);
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(10);
    auto* titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("pageTitle"));
    titleLabel->setProperty("role", "pageTitle");
    m_subtitle = new QLabel;
    m_subtitle->setProperty("role", "pageSubtitle");
    m_subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    // The note sits on the title's baseline, as one line of text would.
    m_layout->addWidget(titleLabel, 0, Qt::AlignBaseline);
    m_layout->addWidget(m_subtitle, 0, Qt::AlignBaseline);
    m_layout->addStretch();
}

void PageHeader::setSubtitle(const QString& text) {
    m_subtitle->setText(text);
}

void PageHeader::setSubtitleToolTip(const QString& text) {
    m_subtitle->setToolTip(text);
}

void PageHeader::addAction(QWidget* widget) {
    m_layout->addWidget(widget, 0, Qt::AlignVCenter);
}

} // namespace kiebitz::app
