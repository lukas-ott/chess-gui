#include "settings/ShortcutsPage.hpp"

#include "settings/SettingsView.hpp"
#include "settings/Shortcuts.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <map>

namespace kiebitz::app {

ShortcutsPage::ShortcutsPage(Shortcuts& shortcuts, QWidget* parent)
    : QWidget(parent), m_shortcuts(shortcuts) {
    auto* box = new QVBoxLayout(this);
    box->setContentsMargins(24, 20, 24, 20);
    box->setSpacing(6);

    auto* top = new QHBoxLayout;
    m_notice = new QLabel;
    m_notice->setObjectName(QStringLiteral("shortcutsNotice"));
    m_notice->setProperty("role", "notice");
    top->addWidget(m_notice, 1);
    auto* resetAll = new QPushButton(tr("Reset All"));
    resetAll->setObjectName(QStringLiteral("shortcutsResetAll"));
    resetAll->setProperty("role", "secondary");
    resetAll->setProperty("compact", true);
    resetAll->setFocusPolicy(Qt::NoFocus);
    connect(resetAll, &QPushButton::clicked, this, [this] {
        m_notice->clear();
        m_shortcuts.resetAll();
    });
    top->addWidget(resetAll);
    box->addLayout(top);
    box->addSpacing(8);

    // The menus in their order, each a group of its commands.
    std::vector<QString> menus;
    std::map<QString, QVBoxLayout*> groups;
    for (const auto& command : m_shortcuts.commands()) {
        if (!groups.contains(command.menu)) {
            menus.push_back(command.menu);
            groups[command.menu] = SettingsView::group(box, command.menu);
        }
        auto* control = new QWidget;
        auto* fields = new QHBoxLayout(control);
        fields->setContentsMargins(0, 0, 0, 0);
        fields->setSpacing(8);
        const Row row{command.id, new QKeySequenceEdit, new QPushButton(tr("Reset"))};
        row.edit->setObjectName(QStringLiteral("shortcut:") + command.id);
        row.edit->setMaximumSequenceLength(1);
        row.edit->setToolTip(tr("Press the new key. ⌫: none. Esc: keep this one."));
        row.edit->setFixedWidth(120);
        if (auto* text = row.edit->findChild<QLineEdit*>()) {
            text->setPlaceholderText(QStringLiteral("–")); // no key (not "Press shortcut")
        }
        row.edit->installEventFilter(this);
        row.reset->setObjectName(QStringLiteral("shortcutReset:") + command.id);
        row.reset->setProperty("role", "link");
        row.reset->setProperty("compact", true);
        row.reset->setFocusPolicy(Qt::NoFocus);
        QSizePolicy keep = row.reset->sizePolicy();
        keep.setRetainSizeWhenHidden(true); // the key fields stay in line
        row.reset->setSizePolicy(keep);
        fields->addWidget(row.edit);
        fields->addWidget(row.reset);
        connect(row.edit, &QKeySequenceEdit::editingFinished, this,
                [this, row] { finish(row, row.edit->keySequence()); });
        connect(row.reset, &QPushButton::clicked, this, [this, id = command.id] {
            m_notice->clear();
            m_shortcuts.reset(id);
        });
        QWidget* line = SettingsView::row(command.title, {}, control);
        line->layout()->setContentsMargins(0, 5, 0, 5); // a long list: tighter than other rows
        groups[command.menu]->addWidget(line);
        m_rows.push_back(row);
    }
    box->addStretch(1);
    connect(&m_shortcuts, &Shortcuts::changed, this, [this] { refresh(); });
    refresh();
}

bool ShortcutsPage::eventFilter(QObject* watched, QEvent* event) {
    // While a key field has focus every shortcut is off, so the key pressed reaches it.
    if (event->type() == QEvent::FocusIn) {
        m_shortcuts.setRecording(true);
    } else if (event->type() == QEvent::FocusOut) {
        m_shortcuts.setRecording(false);
    }
    return QWidget::eventFilter(watched, event);
}

void ShortcutsPage::finish(const Row& row, const QKeySequence& key) {
    // Plain ⌫ or ⌦: no key. Esc: never mind. (Neither is a command's key; see isReserved.)
    const QKeyCombination pressed = key.isEmpty() ? QKeyCombination() : key[0];
    const bool plain = pressed.keyboardModifiers() == Qt::NoModifier;
    if (key == m_shortcuts.key(row.id)) {
        // Unchanged; also what the field reports again as it loses focus: keep the note.
    } else if (plain && pressed.key() == Qt::Key_Escape) {
        m_notice->clear();
    } else if (plain && (pressed.key() == Qt::Key_Backspace || pressed.key() == Qt::Key_Delete)) {
        m_notice->setText(m_shortcuts.set(row.id, {}));
    } else if (!key.isEmpty()) {
        m_notice->setText(m_shortcuts.set(row.id, key));
    }
    refresh(); // a key refused: the field shows the one kept
    row.edit->clearFocus();
}

void ShortcutsPage::refresh() {
    for (const Row& row : m_rows) {
        const QSignalBlocker block(row.edit);
        row.edit->setKeySequence(m_shortcuts.key(row.id));
        row.reset->setVisible(!m_shortcuts.isDefault(row.id));
    }
}

} // namespace kiebitz::app
