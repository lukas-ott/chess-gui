#include "AppStyle.hpp"

#include <QApplication>
#include <QEvent>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

#include <array>
#include <utility>

namespace kiebitz::app {

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
    app.installEventFilter(this);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_appearance == Appearance::System && !m_applying) {
            apply();
        }
    });
    apply();
}

bool AppStyle::eventFilter(QObject* watched, QEvent* event) {
    // A tooltip's window is square; without a see-through background its rounded label would
    // sit on square dark corners. Set before the tooltip first shows (polish comes first).
    if (event->type() == QEvent::Polish && watched->inherits("QTipLabel")) {
        if (auto* tip = qobject_cast<QWidget*>(watched)) {
            tip->setAttribute(Qt::WA_TranslucentBackground);
        }
    }
    return QObject::eventFilter(watched, event);
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
/* One system for every screen: cards have 10 px corners and 16 px padding (style/Layout.hpp);
   every control (button, field, spin box, combo box) is 32 px tall with 8 px corners. */

/* Cards: the rounded panels */
QFrame[card="true"] {
    background: @surface;
    border: 1px solid @border;
    border-radius: 10px;
}
QFrame[card="true"][focused="true"] { border-color: @accent; }
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
    padding: 7px 10px 7px 14px;
    margin: 6px 0 0 6px;
    border: 1px solid transparent;
    border-bottom: none;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
}
QTabBar#documentTabs::tab:selected { color: @text; background: @surface; border-color: @border; }
QTabBar#documentTabs::tab:hover:!selected { color: @text; background: @raised; }
/* The arrows that appear when there are more tabs than fit */
QTabBar#documentTabs::scroller { width: 26px; }
QTabBar#documentTabs QToolButton {
    background: @window;
    border: none;
    border-radius: 6px;
    margin: 6px 0 0 0;
}
QTabBar#documentTabs QToolButton:hover { background: @raised; }

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

QScrollArea#infoScroll, QScrollArea#infoScroll > QWidget > QWidget { background: transparent; }

/* Move list */
QAbstractScrollArea#moveList { background: transparent; border: none; color: @text; }

/* Navigation buttons under the moves */
QPushButton[role="nav"] {
    background: @raised;
    border: none;
    border-radius: 8px;
    min-height: 32px;
}
QPushButton[role="nav"]:hover { background: @border; }
QPushButton[role="nav"]:pressed { background: @dimmed; }
QPushButton[role="nav"]:disabled { background: @raised; }

/* Continuations at a branch point */
QPushButton[role="fork"] {
    background: transparent;
    color: @muted;
    border: none;
    border-radius: 8px;
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

/* Tooltips: a small rounded label (see eventFilter: their window is made see-through so the
   corners can be round) */
QToolTip { background: @raised; color: @text; border: 1px solid @border; border-radius: 7px;
           padding: 5px 9px; font-size: 12px; }

/* Page headers (Library, Engines): style/PageHeader */
QLabel[role="pageTitle"] { color: @text; font-size: 22px; font-weight: 700; }
QLabel[role="pageSubtitle"] { color: @muted; font-size: 13px; }

QLabel[role="sectionTitle"] { color: @text; font-size: 15px; font-weight: 600; }
QLabel[role="previewPlayers"] { color: @text; font-size: 14px; font-weight: 600; }

/* Buttons: 32 px tall (contents 18 px + padding + border) */
/* The main action on a page: one per screen, in the accent colour */
QPushButton[role="primary"] {
    background: @accent;
    color: @accentText;
    border: none;
    border-radius: 8px;
    padding: 7px 16px;
    min-height: 18px;
    font-weight: 600;
}
QPushButton[role="primary"]:hover { background: @accentHover; }
/* Quiet buttons */
QPushButton[role="secondary"] {
    background: @surface;
    color: @text;
    border: 1px solid @border;
    border-radius: 8px;
    padding: 6px 14px;
    min-height: 18px;
}
QPushButton[role="secondary"]:hover { background: @raised; }
QPushButton[role="secondary"]:pressed { background: @border; }
/* Start / Stop of an engine: quiet when off, accent while it runs */
QPushButton[role="engineToggle"] {
    background: @raised;
    color: @text;
    border: 1px solid @border;
    border-radius: 8px;
    padding: 6px 14px;
    min-height: 18px;
    font-weight: 600;
}
QPushButton[role="engineToggle"]:hover { background: @border; }
QPushButton[role="engineToggle"]:checked { background: @accent; color: @accentText; border-color: @accent; }
QPushButton[role="engineToggle"]:checked:hover { background: @accentHover; }
/* Icon-only buttons (32 x 32, set in code) */
QToolButton[role="icon"] {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 8px;
}
QToolButton[role="icon"]:hover { background: @raised; }
QToolButton[role="icon"][outlined="true"] { border-color: @border; background: @surface; }
QToolButton[role="icon"]::menu-indicator { image: none; }

/* Segmented controls (lines per engine) and steppers (− value +): a 32 px track with
   28 px segments inside it; the chosen segment is raised, as in macOS and Lichess pickers */
QWidget[role="segmented"], QWidget[role="stepper"] {
    background: @window;
    border: 1px solid @border;
    border-radius: 8px;
}
QPushButton[role="segment"] {
    background: transparent;
    color: @muted;
    border: none;
    border-radius: 6px;
    min-width: 26px;
    min-height: 26px;
    padding: 0 4px;
    font-weight: 600;
}
QPushButton[role="segment"]:hover { color: @text; background: @raised; }
QPushButton[role="segment"]:checked { color: @text; background: @border; }
QLineEdit[role="stepperValue"] {
    background: transparent;
    border: none;
    padding: 0;
    min-height: 26px;
    font-weight: 600;
}
QPushButton[role="step"] {
    background: transparent;
    color: @muted;
    border: none;
    border-radius: 6px;
    min-width: 26px;
    min-height: 26px;
    font-size: 16px;
    font-weight: 600;
}
QPushButton[role="step"]:hover { color: @text; background: @raised; }
QPushButton[role="step"]:pressed { background: @border; }
/* The text field inside a spin box or editable combo box is part of it: no box of its own */
QAbstractSpinBox QLineEdit, QComboBox QLineEdit {
    background: transparent;
    border: none;
    padding: 0;
    min-height: 0;
}

/* Engine tab */
QLabel[role="engineStatus"] { color: @muted; font-size: 12px; }
QLabel[role="pvLine"] { color: @text; font-size: 14px; }
/* A score chip in the colours of the side that is better, as Lichess shows it */
QLabel[role="scorePill"] {
    border-radius: 6px;
    padding: 2px 0;
    font-size: 13px;
    font-weight: 600;
    background: #F0F0F0;
    color: #2B2926;
}
QLabel[role="scorePill"][side="black"] { background: #403D39; color: #F0F0F0; }
QLabel[role="error"] { color: #E5534B; }
QProgressBar#downloadBar { background: @raised; border: none; border-radius: 3px; }
QProgressBar#downloadBar::chunk { background: @accent; border-radius: 3px; }

/* Sidebar: one icon per section; the current one has an accent bar on its left edge */
QFrame#sidebar { background: @window; border-right: 1px solid @border; }
QToolButton[role="sideNav"] {
    background: transparent;
    border: none;
    border-left: 3px solid transparent;
    border-radius: 0;
}
QToolButton[role="sideNav"]:hover { background: @raised; }
QToolButton[role="sideNav"]:checked { border-left-color: @accent; }

/* Engines page */
QFrame[engineCard="true"] { background: @raised; }
QFrame[engineCard="true"]:hover { border-color: @muted; }
QFrame[engineCard="true"][selected="true"] { border-color: @accent; }
QLabel[role="cardName"] { color: @text; font-size: 15px; font-weight: 600; }
QLabel[role="statTitle"] { color: @muted; font-size: 11px; font-weight: 600; }
QLabel[role="statValue"] { color: @text; font-size: 15px; font-weight: 600; }
QScrollArea#engineCards, QWidget#engineCardsContent { background: transparent; }
QScrollArea#engineSections, QWidget#engineSectionsContent { background: transparent; }
QWidget#engineSection { border-bottom: 1px solid @border; }
QLabel[role="emptyIcon"] { background: @raised; border-radius: 32px; } /* widgets/EmptyState */
/* A button that reads as a link */
QPushButton[role="link"] {
    background: transparent;
    border: none;
    color: @accent;
    padding: 0;
    font-weight: 600;
}
QPushButton[role="link"]:hover { color: @accentHover; }

/* Fields: search, comment box, header fields, spin boxes, combo boxes (32 px tall) */
QLineEdit, QPlainTextEdit#commentBox, QComboBox, QSpinBox {
    background: @window;
    color: @text;
    border: 1px solid @border;
    border-radius: 8px;
    padding: 4px 10px;
    selection-background-color: @accent;
    selection-color: @accentText;
}
QLineEdit, QComboBox, QSpinBox { min-height: 22px; }
QSpinBox { min-width: 70px; }
QLineEdit:focus, QPlainTextEdit#commentBox:focus, QComboBox:focus, QSpinBox:focus {
    border-color: @accent;
}
QLineEdit:disabled { color: @dimmed; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
    background: @surface; color: @text; border: 1px solid @border;
    selection-background-color: @accentTint; selection-color: @accent;
}

/* Library: file list and games table */
QTreeWidget#libraryFiles {
    background: transparent;
    color: @text;
    outline: none;
    show-decoration-selected: 0; /* keep Qt's own fold arrows: styling the branch hides them */
}
QTreeWidget#libraryFiles::item { padding: 5px 2px; border: none; }
QTreeWidget#libraryFiles::item:hover { background: @raised; }
QTreeWidget#libraryFiles::item:selected { background: @accentTint; color: @accent; }
QTableView#libraryGames {
    background: transparent;
    color: @text;
    alternate-background-color: @raised;
    outline: none;
    selection-background-color: @accentTint;
    selection-color: @accent;
}
QTableView#libraryGames::item { padding: 0 6px; border: none; }
QHeaderView::section {
    background: transparent;
    color: @muted;
    border: none;
    border-bottom: 1px solid @border;
    padding: 6px;
    font-size: 11px;
    font-weight: 600;
}

/* Document tabs: close buttons and the "+" button */
/* (Named through the tab bar: the rule for its scroll arrows would otherwise win.) */
QTabBar#documentTabs QToolButton#tabClose {
    background: transparent;
    border: none;
    border-radius: 10px;
    padding: 0;
    /* top: the tabs' own top margin, so it centres on the title; right: it sits a little in
       from the tab's edge, as in a browser */
    margin: 6px 6px 0 0;
}
QTabBar#documentTabs QToolButton#tabClose:hover { background: @raised; }
QToolButton#newTabButton {
    color: @muted;
    background: transparent;
    border: none;
    border-radius: 8px;
    font-size: 18px;
    padding: 0 8px;
    margin-top: 6px;
}
QToolButton#newTabButton:hover { background: @raised; color: @text; }
QWidget#tabRow { background: @window; }

/* Status bar: a quiet line at the bottom */
QStatusBar { background: @window; color: @muted; font-size: 12px; }
QStatusBar::item { border: none; }

)");
    // Placeholders instead of %1..%9: easier to read than positional arguments.
    // The accent mixed 18 % into the card colour: a soft background for chosen items.
    const auto mix = [](const QColor& a, const QColor& b, float t) {
        return QColor::fromRgbF((a.redF() * t) + (b.redF() * (1 - t)),
                                (a.greenF() * t) + (b.greenF() * (1 - t)),
                                (a.blueF() * t) + (b.blueF() * (1 - t)));
    };
    const QColor tint = mix(colors.accent, colors.surface, 0.18F);
    const QColor hover = mix(colors.accent, colors.text, 0.85F);
    const std::array<std::pair<const char*, QColor>, 11> names{{
        {"@window", colors.window},
        {"@surface", colors.surface},
        {"@raised", colors.raised},
        {"@border", colors.border},
        {"@text", colors.text},
        {"@muted", colors.muted},
        {"@dimmed", colors.dimmed},
        {"@accentText", colors.accentText}, // before @accent: it starts with the same letters
        {"@accentTint", tint},
        {"@accentHover", hover},
        {"@accent", colors.accent},
    }};
    for (const auto& [name, color] : names) {
        css.replace(QLatin1String(name), hex(color));
    }
    return css;
}

} // namespace kiebitz::app
