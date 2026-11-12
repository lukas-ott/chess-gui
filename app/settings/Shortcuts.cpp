#include "settings/Shortcuts.hpp"

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QShortcut>
#include <QWidget>

#include <algorithm>

namespace kiebitz::app {
namespace {

QString settingsKey(const QString& id) {
    return QStringLiteral("shortcuts/") + id;
}

} // namespace

Shortcuts::Shortcuts(QWidget* window) : QObject(window), m_window(window) {}

QString Shortcuts::plainText(const QString& text) {
    QString plain = text;
    plain.remove(QLatin1Char('&')).remove(QChar(0x2026)).remove(QStringLiteral("..."));
    plain.replace(QLatin1Char('/'), QLatin1Char('-')); // "/" would nest in the settings
    return plain.simplified();
}

void Shortcuts::add(QAction* action, const QString& menu, bool game) {
    Command command;
    command.menu = plainText(menu);
    command.title = plainText(action->text());
    command.id = command.menu + QLatin1Char('.') + command.title;
    command.action = action;
    command.defaultKey = action->shortcut();
    command.game = game;
    // A command outside the menus (↑ ↓) needs a widget for its shortcut to work.
    if (action->associatedObjects().isEmpty()) {
        m_window->addAction(action);
    }
    m_commands.push_back(command);
    apply(m_commands.back(), command.defaultKey);
}

void Shortcuts::addMenus(const QMenuBar* bar, const std::vector<QAction*>& game) {
    for (const QAction* top : bar->actions()) {
        const QMenu* menu = top->menu();
        if (menu == nullptr) {
            continue;
        }
        for (QAction* action : menu->actions()) {
            // Not separators or submenus, nor one choice of a list (a board colour).
            if (action->isSeparator() || action->menu() != nullptr ||
                action->actionGroup() != nullptr || action->text().isEmpty()) {
                continue;
            }
            add(action, top->text(), std::find(game.begin(), game.end(), action) != game.end());
        }
    }
}

void Shortcuts::load() {
    const QSettings settings;
    for (Command& command : m_commands) {
        const QString key = settingsKey(command.id);
        if (!settings.contains(key)) {
            continue;
        }
        const QKeySequence saved =
            QKeySequence::fromString(settings.value(key).toString(), QKeySequence::PortableText);
        // A saved key that another command has since got by default: the default stays.
        const bool taken =
            std::any_of(m_commands.begin(), m_commands.end(), [&](const Command& other) {
                return &other != &command && !saved.isEmpty() && other.key == saved;
            });
        if (!taken && !isReserved(saved)) {
            apply(command, saved);
        }
    }
}

QKeySequence Shortcuts::key(const QString& id) const {
    const Command* command = find(id);
    return command != nullptr ? command->key : QKeySequence();
}

bool Shortcuts::isDefault(const QString& id) const {
    const Command* command = find(id);
    return command == nullptr || command->key == command->defaultKey;
}

QString Shortcuts::set(const QString& id, const QKeySequence& key) {
    Command* command = find(id);
    if (command == nullptr) {
        return {};
    }
    // One key, not a sequence of several ("⌘K, ⌘C"): the macOS menu bar can't show those.
    const QKeySequence single = key.isEmpty() ? QKeySequence() : QKeySequence(key[0]);
    if (isReserved(single)) {
        return tr("%1 is kept for text fields.").arg(single.toString(QKeySequence::NativeText));
    }
    for (const Command& other : m_commands) {
        if (&other != command && !single.isEmpty() && other.key == single) {
            return tr("%1 is used by %2.")
                .arg(single.toString(QKeySequence::NativeText), other.title);
        }
    }
    apply(*command, single);
    QSettings settings;
    if (single == command->defaultKey) {
        settings.remove(settingsKey(id));
    } else {
        settings.setValue(settingsKey(id), single.toString(QKeySequence::PortableText));
    }
    emit changed(id);
    return {};
}

void Shortcuts::reset(const QString& id) {
    Command* command = find(id);
    if (command == nullptr) {
        return;
    }
    // Back to its default, unless another command has that key now: then it has none.
    const bool taken = std::any_of(m_commands.begin(), m_commands.end(), [&](const Command& other) {
        return &other != command && !command->defaultKey.isEmpty() &&
               other.key == command->defaultKey;
    });
    if (taken) {
        set(id, {});
        return;
    }
    apply(*command, command->defaultKey);
    QSettings().remove(settingsKey(id));
    emit changed(id);
}

void Shortcuts::resetAll() {
    for (Command& command : m_commands) {
        apply(command, command.defaultKey);
    }
    QSettings().remove(QStringLiteral("shortcuts"));
    emit changed({});
}

void Shortcuts::setGameKeysEnabled(bool on) {
    m_gameOn = on;
    for (const Command& command : m_commands) {
        updatePlain(command);
    }
}

void Shortcuts::setRecording(bool on) {
    if (m_recording == on) {
        return;
    }
    m_recording = on;
    for (Command& command : m_commands) {
        apply(command, command.key); // the actions lose their keys meanwhile, then get them back
    }
}

bool Shortcuts::isReserved(const QKeySequence& key) {
    if (key.isEmpty()) {
        return false;
    }
    for (const auto standard : {QKeySequence::Copy, QKeySequence::Cut, QKeySequence::SelectAll,
                                QKeySequence::Undo, QKeySequence::Redo}) {
        if (QKeySequence::keyBindings(standard).contains(key)) {
            return true;
        }
    }
    // ⌘H and ⌘M hide and minimise every Mac app; Tab, Return, Esc, ⌫ belong to fields and dialogs.
    const QKeyCombination combination = key[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    const Qt::Key k = combination.key();
    if (modifiers == Qt::ControlModifier && (k == Qt::Key_H || k == Qt::Key_M)) {
        return true;
    }
    const bool plain = (modifiers & ~Qt::ShiftModifier) == Qt::NoModifier;
    return plain &&
           (k == Qt::Key_Tab || k == Qt::Key_Backtab || k == Qt::Key_Return || k == Qt::Key_Enter ||
            k == Qt::Key_Escape || k == Qt::Key_Backspace || k == Qt::Key_Delete);
}

bool Shortcuts::needsWindowKey(const QKeySequence& key) {
    if (key.isEmpty()) {
        return false;
    }
    const QKeyCombination combination = key[0];
    if ((combination.keyboardModifiers() &
         (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) != Qt::NoModifier) {
        return false;
    }
    // Characters (F, 1, [) work in the menu bar; Space and the special keys don't.
    const int k = combination.key();
    return k == Qt::Key_Space || k >= Qt::Key_Escape;
}

Shortcuts::Command* Shortcuts::find(const QString& id) {
    const auto it = std::find_if(m_commands.begin(), m_commands.end(),
                                 [&](const Command& c) { return c.id == id; });
    return it != m_commands.end() ? &*it : nullptr;
}

const Shortcuts::Command* Shortcuts::find(const QString& id) const {
    const auto it = std::find_if(m_commands.begin(), m_commands.end(),
                                 [&](const Command& c) { return c.id == id; });
    return it != m_commands.end() ? &*it : nullptr;
}

void Shortcuts::apply(Command& command, const QKeySequence& key) {
    command.key = key;
    const bool window = needsWindowKey(key);
    // While recording the action has no key at all, so the menu bar can't run it.
    command.action->setShortcut(m_recording ? QKeySequence() : key);
    // A key the menu bar ignores: the menu item only shows it (WidgetShortcut keeps its own
    // copy from firing as well), the window's shortcut runs it.
    command.action->setShortcutContext(window ? Qt::WidgetShortcut : Qt::WindowShortcut);
    if (window && command.plain == nullptr) {
        command.plain = new QShortcut(m_window);
        command.plain->setContext(Qt::WindowShortcut);
        QAction* action = command.action;
        connect(command.plain, &QShortcut::activated, action, [action] {
            if (action->isEnabled()) {
                action->trigger();
            }
        });
    }
    if (command.plain != nullptr) {
        command.plain->setKey(window ? key : QKeySequence());
    }
    updatePlain(command);
}

void Shortcuts::updatePlain(const Command& command) const {
    if (command.plain != nullptr) {
        command.plain->setEnabled(!m_recording && (!command.game || m_gameOn) &&
                                  !command.plain->key().isEmpty());
    }
}

} // namespace kiebitz::app
