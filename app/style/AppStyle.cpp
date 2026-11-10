#include "AppStyle.hpp"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

#include <array>
#include <utility>

namespace chessgui::app {

AppStyle::Colors AppStyle::darkColors() {
    return {
        .window = QColor(0x1A, 0x1B, 0x1E),
        .surface = QColor(0x25, 0x26, 0x2B),
        .raised = QColor(0x2C, 0x2E, 0x33),
        .border = QColor(0x37, 0x3A, 0x40),
        .text = QColor(0xC1, 0xC2, 0xC5),
        .muted = QColor(0x90, 0x92, 0x96),
        .dimmed = QColor(0x5C, 0x5F, 0x66),
        .accent = QColor(0x4D, 0xAB, 0xF7),
        .accentText = QColor(0x10, 0x11, 0x13),
    };
}

AppStyle::Colors AppStyle::lightColors() {
    return {
        .window = QColor(0xF1, 0xF3, 0xF5),
        .surface = QColor(0xFF, 0xFF, 0xFF),
        .raised = QColor(0xF1, 0xF3, 0xF5),
        .border = QColor(0xDE, 0xE2, 0xE6),
        .text = QColor(0x21, 0x25, 0x29),
        .muted = QColor(0x86, 0x8E, 0x96),
        .dimmed = QColor(0xAD, 0xB5, 0xBD),
        .accent = QColor(0x22, 0x8B, 0xE6),
        .accentText = QColor(0xFF, 0xFF, 0xFF),
    };
}

AppStyle::AppStyle(QApplication& app) : QObject(&app), m_app(app) {
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_appearance == Appearance::System && !m_applying) {
            apply();
        }
    });
    apply();
}

void AppStyle::setAppearance(Appearance appearance) {
    m_appearance = appearance;
    apply();
}

QString AppStyle::toString(Appearance appearance) {
    switch (appearance) {
    case Appearance::Light:
        return QStringLiteral("light");
    case Appearance::Dark:
        return QStringLiteral("dark");
    case Appearance::System:
        break;
    }
    return QStringLiteral("system");
}

AppStyle::Appearance AppStyle::fromString(const QString& text) {
    if (text == QLatin1String("light")) {
        return Appearance::Light;
    }
    if (text == QLatin1String("dark")) {
        return Appearance::Dark;
    }
    return Appearance::System;
}

void AppStyle::apply() {
    // First tell the system which appearance we want, so its own parts (title bar, menus,
    // dialogs) match; with System, hand the choice back to macOS. This emits colorSchemeChanged,
    // and the guard keeps that from re-entering apply().
    auto* hints = QGuiApplication::styleHints();
    m_applying = true;
    switch (m_appearance) {
    case Appearance::Light:
        hints->setColorScheme(Qt::ColorScheme::Light);
        break;
    case Appearance::Dark:
        hints->setColorScheme(Qt::ColorScheme::Dark);
        break;
    case Appearance::System:
        hints->unsetColorScheme();
        break;
    }
    m_applying = false;
    // A forced choice is used as is; with System, ask macOS (unknown counts as dark, the app's
    // home look).
    m_dark = m_appearance == Appearance::Dark ||
             (m_appearance == Appearance::System && hints->colorScheme() != Qt::ColorScheme::Light);
    m_colors = m_dark ? darkColors() : lightColors();
    const Colors& c = m_colors;

    QPalette palette;
    palette.setColor(QPalette::Window, c.window);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, c.surface);
    palette.setColor(QPalette::AlternateBase, c.raised);
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::PlaceholderText, c.muted);
    palette.setColor(QPalette::Button, c.surface);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::Highlight, c.accent);
    palette.setColor(QPalette::HighlightedText, c.accentText);
    palette.setColor(QPalette::ToolTipBase, c.raised);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::Link, c.accent);
    palette.setColor(QPalette::Mid, c.border);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, c.dimmed);
    }
    QApplication::setPalette(palette);
    m_app.setStyleSheet(styleSheet(c));
    emit changed();
}

QString AppStyle::styleSheet(const Colors& colors) {
    const auto hex = [](const QColor& color) {
        return color.name(QColor::HexRgb);
    };
    QString css = QStringLiteral(R"(
/* Cards: the rounded panels on the right */
QFrame[card="true"] {
    background: @surface;
    border: 1px solid @border;
    border-radius: 8px;
}
QLabel[role="cardTitle"] {
    color: @muted;
    font-size: 11px;
    font-weight: 600;
}
QLabel[role="emptyTitle"] { color: @text; font-size: 15px; font-weight: 600; }
QLabel[role="emptyHint"] { color: @muted; font-size: 12px; }

/* Document tabs along the top */
QTabBar#documentTabs { background: @window; }
QTabBar#documentTabs::tab {
    color: @muted;
    background: transparent;
    padding: 7px 16px;
    margin: 6px 0 0 6px;
    border: 1px solid transparent;
    border-bottom: none;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
}
QTabBar#documentTabs::tab:selected { color: @text; background: @surface; border-color: @border; }
QTabBar#documentTabs::tab:hover:!selected { color: @text; background: @raised; }

/* Panel tabs: text with an accent underline */
QTabWidget#panels::pane { border: none; }
QTabWidget#panels > QTabBar { background: transparent; border-bottom: 1px solid @border; }
QTabWidget#panels > QTabBar::tab {
    color: @muted;
    background: transparent;
    padding: 9px 12px;
    border: none;
    border-bottom: 2px solid transparent;
}
QTabWidget#panels > QTabBar::tab:selected { color: @text; border-bottom-color: @accent; }
QTabWidget#panels > QTabBar::tab:hover:!selected { color: @text; }

/* Move list */
QTextBrowser#moveList { background: transparent; border: none; color: @text; }

/* Navigation buttons under the moves */
QPushButton[role="nav"] {
    background: @raised;
    border: none;
    border-radius: 6px;
    min-height: 34px;
}
QPushButton[role="nav"]:hover { background: @border; }
QPushButton[role="nav"]:pressed { background: @dimmed; }
QPushButton[role="nav"]:disabled { background: @raised; }

/* Continuations at a branch point */
QPushButton[role="fork"] {
    background: transparent;
    color: @muted;
    border: none;
    border-radius: 6px;
    padding: 6px 10px;
    text-align: left;
    font-size: 14px;
}
QPushButton[role="fork"]:hover { background: @raised; color: @text; }
QPushButton[role="fork"][chosen="true"] { background: @accentTint; color: @accent; font-weight: 600; }
QLabel[role="hint"] { color: @muted; font-size: 12px; padding: 2px 2px 4px 2px; }

/* Splitters: invisible gaps */
QSplitter::handle { background: transparent; }
QSplitter::handle:horizontal { width: 8px; }
QSplitter::handle:vertical { height: 8px; }

/* Slim scroll bars */
QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 2px; }
QScrollBar::handle { background: @border; border-radius: 3px; min-height: 24px; min-width: 24px; }
QScrollBar::handle:hover { background: @dimmed; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QToolTip { background: @raised; color: @text; border: 1px solid @border; padding: 4px 6px; }
)");
    // Placeholders instead of %1..%9: easier to read than positional arguments.
    // The accent mixed 18 % into the card colour: a soft background for chosen items.
    const auto mix = [](const QColor& a, const QColor& b, float t) {
        return QColor::fromRgbF((a.redF() * t) + (b.redF() * (1 - t)),
                                (a.greenF() * t) + (b.greenF() * (1 - t)),
                                (a.blueF() * t) + (b.blueF() * (1 - t)));
    };
    const QColor tint = mix(colors.accent, colors.surface, 0.18F);
    const std::array<std::pair<const char*, QColor>, 10> names{{
        {"@window", colors.window},
        {"@surface", colors.surface},
        {"@raised", colors.raised},
        {"@border", colors.border},
        {"@text", colors.text},
        {"@muted", colors.muted},
        {"@dimmed", colors.dimmed},
        {"@accentText", colors.accentText}, // before @accent: it starts with the same letters
        {"@accentTint", tint},
        {"@accent", colors.accent},
    }};
    for (const auto& [name, color] : names) {
        css.replace(QLatin1String(name), hex(color));
    }
    return css;
}

} // namespace chessgui::app
