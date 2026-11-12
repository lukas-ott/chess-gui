#include "AddAccountDialog.hpp"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace kiebitz::app {

AddAccountDialog::AddAccountDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Add Account"));
    setObjectName(QStringLiteral("addAccountDialog"));
    setMinimumWidth(400);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(8);
    const auto title = [](const QString& text) {
        auto* result = new QLabel(text);
        result->setProperty("role", "cardTitle");
        return result;
    };
    layout->addWidget(title(tr("SITE")));
    auto* segments = new QWidget;
    segments->setProperty("role", "segmented");
    auto* row = new QHBoxLayout(segments);
    row->setContentsMargins(2, 2, 2, 2);
    row->setSpacing(2);
    m_sites = new QButtonGroup(this);
    for (const auto& [index, text] :
         {std::pair{0, QStringLiteral("Lichess")}, std::pair{1, QStringLiteral("chess.com")}}) {
        auto* segment = new QPushButton(text);
        segment->setProperty("role", "segment");
        segment->setCheckable(true);
        segment->setChecked(index == 0);
        segment->setFocusPolicy(Qt::NoFocus);
        segment->setFixedWidth(96);
        m_sites->addButton(segment, index);
        row->addWidget(segment);
    }
    layout->addWidget(segments, 0, Qt::AlignLeft);
    layout->addSpacing(6);
    layout->addWidget(title(tr("USERNAME")));
    m_username = new QLineEdit;
    m_username->setObjectName(QStringLiteral("addAccountUsername"));
    m_username->setPlaceholderText(tr("Their username on the site"));
    layout->addWidget(m_username);
    auto* hint = new QLabel(tr("Their public games are downloaded; no login needed."));
    hint->setProperty("role", "emptyHint");
    layout->addWidget(hint);
    layout->addSpacing(12);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new QPushButton(tr("Cancel"));
    cancel->setProperty("role", "secondary");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto* add = new QPushButton(tr("Add"));
    add->setObjectName(QStringLiteral("addAccountOk"));
    add->setProperty("role", "primary");
    add->setDefault(true);
    add->setEnabled(false);
    connect(add, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_username, &QLineEdit::textChanged, this,
            [add](const QString& text) { add->setEnabled(!text.trimmed().isEmpty()); });
    buttons->addWidget(cancel);
    buttons->addWidget(add);
    layout->addLayout(buttons);
}

AccountSite AddAccountDialog::site() const {
    return m_sites->checkedId() == 1 ? AccountSite::ChessCom : AccountSite::Lichess;
}

QString AddAccountDialog::username() const {
    return m_username->text().trimmed();
}

} // namespace kiebitz::app
