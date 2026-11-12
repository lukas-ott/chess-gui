#pragma once

#include <QDialog>
#include <QString>

#include <cstdint>

class QWidget;

namespace kiebitz::app {

/// The app's own message, in its look: a title, what it means (and more, quieter, if need be),
/// and buttons named after what they do. Used everywhere instead of QMessageBox, whose grey box
/// with its "?" and Yes/No looks out of place.
class MessageDialog : public QDialog {
    Q_OBJECT

public:
    enum class Action : std::uint8_t {
        Normal,      ///< the action in the accent colour
        Destructive, ///< in red: it deletes or removes something
    };
    /// `action` empty: a notice with one "OK" button. Else Cancel and `action`.
    MessageDialog(const QString& title, const QString& text, const QString& detail,
                  const QString& action, Action kind, QWidget* parent);
};

/// A notice: something happened or went wrong. One "OK".
void notice(QWidget* parent, const QString& title, const QString& text, const QString& detail = {});
/// A question with Cancel and an action named for what it does ("Remove", "Add"). True if
/// the action was chosen; Cancel is the default (Return, Escape).
[[nodiscard]] bool confirm(QWidget* parent, const QString& title, const QString& text,
                           const QString& action,
                           MessageDialog::Action kind = MessageDialog::Action::Normal,
                           const QString& detail = {});

} // namespace kiebitz::app
