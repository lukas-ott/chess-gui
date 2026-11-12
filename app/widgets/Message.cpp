#include "Message.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace kiebitz::app {

MessageDialog::MessageDialog(const QString& title, const QString& text, const QString& detail,
                             const QString& action, Action kind, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("messageDialog"));
    setWindowTitle(title);
    setMinimumWidth(400);
    setMaximumWidth(520);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(8);
    auto* heading = new QLabel(title);
    heading->setObjectName(QStringLiteral("messageTitle"));
    heading->setProperty("role", "sectionTitle");
    layout->addWidget(heading);
    auto* body = new QLabel(text);
    body->setObjectName(QStringLiteral("messageText"));
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    body->setOpenExternalLinks(true);
    layout->addWidget(body);
    if (!detail.isEmpty()) {
        auto* more = new QLabel(detail);
        more->setProperty("role", "emptyHint");
        more->setWordWrap(true);
        layout->addWidget(more);
    }
    layout->addSpacing(12);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch();
    if (action.isEmpty()) {
        auto* ok = new QPushButton(tr("OK"));
        ok->setObjectName(QStringLiteral("messageOk"));
        ok->setProperty("role", "primary");
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        buttons->addWidget(ok);
    } else {
        auto* cancel = new QPushButton(tr("Cancel"));
        cancel->setObjectName(QStringLiteral("messageCancel"));
        cancel->setProperty("role", "secondary");
        cancel->setDefault(true); // nothing happens by accident
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        auto* act = new QPushButton(action);
        act->setObjectName(QStringLiteral("messageAction"));
        act->setProperty("role", kind == Action::Destructive ? "danger" : "primary");
        act->setAutoDefault(false);
        connect(act, &QPushButton::clicked, this, &QDialog::accept);
        buttons->addWidget(cancel);
        buttons->addWidget(act);
    }
    layout->addLayout(buttons);
}

void notice(QWidget* parent, const QString& title, const QString& text, const QString& detail) {
    MessageDialog dialog(title, text, detail, {}, MessageDialog::Action::Normal, parent);
    dialog.exec();
}

bool confirm(QWidget* parent, const QString& title, const QString& text, const QString& action,
             MessageDialog::Action kind, const QString& detail) {
    MessageDialog dialog(title, text, detail, action, kind, parent);
    return dialog.exec() == QDialog::Accepted;
}

} // namespace kiebitz::app
