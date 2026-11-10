#include "BoardTheme.hpp"

namespace chessgui::app {

QList<BoardTheme> BoardTheme::presets() {
    // name, light squares, dark squares, highlight
    return {
        {QStringLiteral("Slate"), QColor(0xE6, 0xEA, 0xEE), QColor(0x87, 0x96, 0xA5),
         QColor(0x5B, 0x9B, 0xE6, 0x5C)},
        {QStringLiteral("Brown"), QColor(0xF0, 0xD9, 0xB5), QColor(0xB5, 0x88, 0x63),
         QColor(0x9B, 0xC7, 0x00, 0x69)},
        {QStringLiteral("Green"), QColor(0xEB, 0xEC, 0xD0), QColor(0x73, 0x95, 0x52),
         QColor(0xFF, 0xFF, 0x33, 0x73)},
        {QStringLiteral("Blue"), QColor(0xDE, 0xE3, 0xE6), QColor(0x8C, 0xA2, 0xAD),
         QColor(0x9B, 0xC7, 0x00, 0x69)},
        {QStringLiteral("Grey"), QColor(0xD9, 0xD9, 0xD9), QColor(0x9A, 0x9A, 0x9A),
         QColor(0xFF, 0xD8, 0x4D, 0x73)},
    };
}

BoardTheme BoardTheme::preset(const QString& name) {
    const auto all = presets();
    for (const auto& theme : all) {
        if (theme.name == name) {
            return theme;
        }
    }
    return all.front();
}

BoardTheme BoardTheme::custom(const QColor& light, const QColor& dark) {
    return {QStringLiteral("Custom"), light, dark, QColor(0xFF, 0xD8, 0x4D, 0x73)};
}

} // namespace chessgui::app
