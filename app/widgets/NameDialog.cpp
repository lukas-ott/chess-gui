#include "NameDialog.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace kiebitz::app {

NameDialog::NameDialog(const QString& title, const QString& action, const QString& placeholder,
                       const QString& text, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(title);
    setObjectName(QStringLiteral("nameDialog"));
    setMinimumWidth(380);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(8);
    auto* heading = new QLabel(tr("NAME"));
    heading->setProperty("role", "cardTitle");
    layout->addWidget(heading);
    m_name = new QLineEdit(text);
    m_name->setObjectName(QStringLiteral("nameDialogName"));
    m_name->setPlaceholderText(placeholder);
    m_name->selectAll();
    layout->addWidget(m_name);
    layout->addSpacing(12);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new QPushButton(tr("Cancel"));
    cancel->setProperty("role", "secondary");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto* ok = new QPushButton(action);
    ok->setObjectName(QStringLiteral("nameDialogOk"));
    ok->setProperty("role", "primary");
    ok->setDefault(true);
    ok->setEnabled(!text.trimmed().isEmpty());
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_name, &QLineEdit::textChanged, this,
            [ok](const QString& value) { ok->setEnabled(!value.trimmed().isEmpty()); });
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    layout->addLayout(buttons);
}

QString NameDialog::name() const {
    return m_name->text().trimmed();
}

} // namespace kiebitz::app
