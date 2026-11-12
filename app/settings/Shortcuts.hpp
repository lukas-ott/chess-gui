#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>

#include <vector>

class QAction;
class QMenuBar;
class QShortcut;
class QWidget;

namespace kiebitz::app {

/// The keyboard shortcuts of the window's commands: their defaults, the ones the user set
/// (kept in the settings under "shortcuts/<id>"), and clashes between them.
///
/// The macOS menu bar ignores keys without ⌘, ⌃ or ⌥ that aren't characters (← → Home End
/// Space). A command with such a key gets a window shortcut for it as well, and its menu item
/// keeps the key only to show it. Game commands' window shortcuts are on only while a game
/// is shown (setGameKeysEnabled()), so in the Library the arrows belong to the lists.
class Shortcuts : public QObject {
    Q_OBJECT

public:
    struct Command {
        QString id;    ///< "Game.Back": the menu and the command, in English; never changes
        QString menu;  ///< "Game", as shown
        QString title; ///< "Back", as shown
        QAction* action = nullptr;
        QKeySequence defaultKey;
        QKeySequence key;           ///< the key now (the action has none while recording)
        QShortcut* plain = nullptr; ///< the window's copy for keys the menu bar ignores
        bool game = false;          ///< only on a game
    };

    explicit Shortcuts(QWidget* window);

    /// A command: its id comes from `menu` and the action's text, its default from the
    /// action's shortcut now. `game`: only while a game is shown.
    void add(QAction* action, const QString& menu, bool game);
    /// Every command in the menu bar's menus (not submenus, not the choices of a list such as
    /// the board colours). `game`: the ones that need a game.
    void addMenus(const QMenuBar* bar, const std::vector<QAction*>& game);
    /// The keys the user set, from the settings. After add() / addMenus().
    void load();

    [[nodiscard]] const std::vector<Command>& commands() const { return m_commands; }
    [[nodiscard]] QKeySequence key(const QString& id) const;
    [[nodiscard]] bool isDefault(const QString& id) const;

    /// Sets a command's key (empty: none). Returns why it can't be ("⌘S is used by Save."),
    /// or an empty string when it was set.
    QString set(const QString& id, const QKeySequence& key);
    void reset(const QString& id);
    void resetAll();

    /// Off while a game isn't shown or practice runs (the arrows then belong elsewhere).
    void setGameKeysEnabled(bool on);
    /// While a new key is pressed in Settings: every shortcut off, so the key reaches the field
    /// instead of running its command.
    void setRecording(bool on);

    /// Keys that only a text field may have (copy, undo, ...): never given to a command.
    [[nodiscard]] static bool isReserved(const QKeySequence& key);
    /// Whether the macOS menu bar ignores `key`, so the window must handle it.
    [[nodiscard]] static bool needsWindowKey(const QKeySequence& key);
    /// "Start / Stop &Engine…" → "Start - Stop Engine": a command's name for its id.
    [[nodiscard]] static QString plainText(const QString& text);

signals:
    /// A command's key changed (`id`), or all of them (empty).
    void changed(const QString& id);

private:
    Command* find(const QString& id);
    [[nodiscard]] const Command* find(const QString& id) const;
    void apply(Command& command, const QKeySequence& key);
    void updatePlain(const Command& command) const;

    QWidget* m_window;
    std::vector<Command> m_commands;
    bool m_gameOn = true;
    bool m_recording = false;
};

} // namespace kiebitz::app
