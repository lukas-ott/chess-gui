#pragma once

#include <QColor>
#include <QList>
#include <QString>

namespace kiebitz::app {

/// The board's colours: the two square colours plus a semi-transparent highlight for the last
/// move and the selected piece, chosen to sit well on those squares.
struct BoardTheme {
    QString name; ///< preset name, or "Custom"
    QColor light;
    QColor dark;
    QColor highlight;

    /// The built-in presets. The first one is the default.
    [[nodiscard]] static QList<BoardTheme> presets();
    /// A preset by name, or the default if there is none with that name.
    [[nodiscard]] static BoardTheme preset(const QString& name);
    [[nodiscard]] static BoardTheme custom(const QColor& light, const QColor& dark);
};

} // namespace kiebitz::app
