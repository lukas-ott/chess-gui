#include "AppStyle.hpp"

#include "Icons.hpp"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QImage>
#include <QMenu>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyledItemDelegate>
#include <QWidget>

#include <algorithm>
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
        .good = QColor(0x51, 0xCF, 0x66),
        .bad = QColor(0xFF, 0x6B, 0x6B),
        .whiteWins = QColor(0xE8, 0xE6, 0xE1),
        .draws = QColor(0x80, 0x83, 0x89),
        .blackWins = QColor(0x10, 0x11, 0x13),
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
        .good = QColor(0x2F, 0x9E, 0x44),
        .bad = QColor(0xE0, 0x31, 0x31),
        .whiteWins = QColor(0xFF, 0xFF, 0xFF),
        .draws = QColor(0xAD, 0xB5, 0xBD),
        .blackWins = QColor(0x34, 0x3A, 0x40),
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
    if (event->type() == QEvent::Polish) {
        // Dropdown lists: rows drawn by the style sheet (the default delegate paints them in
        // the system highlight), and a see-through window so the list's corners can be round.
        if (auto* combo = qobject_cast<QComboBox*>(watched);
            combo != nullptr && !combo->property("styledRows").toBool()) {
            combo->setProperty("styledRows", true);
            combo->setItemDelegate(new QStyledItemDelegate(combo));
        }
        if (auto* menu = qobject_cast<QMenu*>(watched)) {
            menu->setAttribute(Qt::WA_TranslucentBackground);
            menu->setWindowFlag(Qt::NoDropShadowWindowHint, false);
        }
        if (watched->inherits("QComboBoxPrivateContainer")) {
            if (auto* frame = qobject_cast<QFrame*>(watched)) {
                frame->setAttribute(Qt::WA_TranslucentBackground);
                frame->setFrameShape(QFrame::NoFrame);
                frame->setContentsMargins(0, 0, 0, 0);
            }
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

void AppStyle::setAccent(const QColor& accent) {
    m_accent = accent;
    apply();
}

void AppStyle::setTextScale(int percent) {
    m_textScale = std::clamp(percent, 80, 140);
    apply();
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
    if (m_accent.isValid()) {
        // Your accent; the text on it light or dark, whichever reads better.
        m_colors.accent = m_accent;
        m_colors.accentText =
            m_accent.lightnessF() > 0.62F ? QColor(QStringLiteral("#111318")) : QColor(Qt::white);
    }
    const Colors& c = m_colors;
    // The text size: the app's font, and every size in the style sheet.
    if (m_baseFont <= 0) {
        m_baseFont = QApplication::font().pointSizeF();
    }
    QFont font = QApplication::font();
    font.setPointSizeF(m_baseFont * m_textScale / 100.0);
    QApplication::setFont(font);

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
    m_app.setStyleSheet(styleSheet(c, writeCheckImage(c.accent), m_textScale));
    emit changed();
}

QString AppStyle::writeCheckImage(const QColor& color) {
    if (!m_images) {
        m_images = std::make_unique<QTemporaryDir>();
    }
    if (!m_images->isValid()) {
        return {};
    }
    // One file per colour (a style sheet may cache an image by its path); a @2x one for Retina.
    const QString name = QStringLiteral("check-%1").arg(color.name(QColor::HexRgb).mid(1));
    const QString path = m_images->filePath(name + QStringLiteral(".png"));
    if (!QFileInfo::exists(path)) {
        for (const int scale : {1, 2}) {
            const int size = 14 * scale;
            const QImage image =
                makeIcon(IconKind::Check, color, color, size)
                    .pixmap(size, size)
                    .toImage()
                    .scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            image.save(m_images->filePath(
                name + (scale == 2 ? QStringLiteral("@2x.png") : QStringLiteral(".png"))));
        }
    }
    return path;
}

QString AppStyle::styleSheet(const Colors& colors, const QString& checkImage, int textScale) {
    const auto hex = [](const QColor& color) {
        return color.name(QColor::HexRgb);
    };
    QString css = QStringLiteral(R"(
/* One system for every screen: cards have 10 px corners and 16 px padding (style/Layout.hpp);
   every control (button, field, spin box, combo box) is 32 px tall with 8 px corners. */

/* Menus (right-click and button menus; the menu bar is the system's): like the dropdowns,
   rounded, in the card colour, a soft highlight, and a tick only on what is chosen. */
QMenu {
    background: @surface;
    color: @text;
    border: 1px solid @border;
    border-radius: 8px;
    padding: 4px;
    font-size: 13px;
}
QMenu::item { padding: 5px 22px 5px 10px; border-radius: 6px; background: transparent; }
QMenu::item:selected { background: @accentTint; color: @accent; }
QMenu::item:disabled { color: @dimmed; }
QMenu::separator { height: 1px; background: @border; margin: 4px 6px; }
QMenu::indicator { width: 14px; height: 14px; padding-left: 6px; }
QMenu::indicator:unchecked { image: none; }
QMenu::indicator:checked { image: url(@checkImage); }

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
QScrollArea#continuations, QWidget#continuationsContent { background: transparent; }

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
QToolTip { background: @text; color: @window; border: none; border-radius: 5px;
           padding: 4px 8px; font-size: 12px; }

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
/* An action that deletes or removes something (in a question): red */
QPushButton[role="danger"] {
    background: @bad;
    color: @accentText;
    border: none;
    border-radius: 8px;
    padding: 7px 16px;
    min-height: 18px;
    font-weight: 600;
}
QPushButton[role="danger"]:hover { background: @badTint; color: @bad; }
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

/* Repertoire: the "3 to check" button opens a menu of problems; a small pill */
QToolButton#repertoireProblems {
    background: @raised; color: @text; border: 1px solid @border; border-radius: 6px;
    padding: 3px 9px; font-size: 12px;
}
QToolButton#repertoireProblems:hover { border-color: @accent; }
QToolButton#repertoireProblems::menu-indicator { image: none; width: 0; }

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

/* Practice: the status (green when right, red when wrong), the file's explanation as a quote,
   and a thin bar for the lines done. */
QLabel[role="practiceStatus"] { color: @text; font-size: 15px; font-weight: 600; }
QLabel[role="practiceStatus"][mood="good"] { color: @good; }
QLabel[role="practiceStatus"][mood="bad"] { color: @bad; }
QLabel[role="quote"] {
    color: @muted;
    font-size: 13px;
    font-style: italic;
    border-left: 2px solid @dimmed;
    padding: 2px 0 2px 10px;
}
QProgressBar#practiceBar { background: @raised; border: none; border-radius: 2px; }
QProgressBar#practiceBar::chunk { background: @accent; border-radius: 2px; }
QProgressBar#puzzleProgress { background: @raised; border: none; border-radius: 2px; }
QProgressBar#puzzleProgress::chunk { background: @accent; border-radius: 2px; }

/* The players' clocks above and below the board */
QLabel[role="boardClock"] {
    background: @surface;
    color: @text;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 0 8px;
    font-size: 13px;
    font-weight: 600;
}

/* Repertoire check: a dot for how the game stands (in book, left it, not covered, ended), the
   headline beside it, and the repertoire's moves as chips that play them */
QLabel[role="checkDot"] { border-radius: 4px; background: @dimmed; }
QLabel[role="checkDot"][mood="good"] { background: @good; }
QLabel[role="checkDot"][mood="bad"] { background: @bad; }
QLabel[role="checkDot"][mood="open"] { background: @accent; }
QLabel[role="checkHeadline"] { color: @text; font-size: 14px; font-weight: 600; }
QPushButton[role="moveChip"] {
    background: @surface;
    color: @text;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 2px 9px;
    min-height: 18px;
    font-weight: 600;
}
QPushButton[role="moveChip"]:hover { border-color: @accent; color: @accent; }
QPushButton[role="primary"][compact="true"] {
    padding: 4px 12px;
    min-height: 16px;
    font-size: 12px;
    border-radius: 6px;
}
QPushButton[role="secondary"][compact="true"] {
    padding: 3px 10px;
    min-height: 16px;
    font-size: 12px;
    border-radius: 6px;
}

/* The date range: a button (accent while a range is set) and its picker's calendars */
QPushButton[active="true"] { color: @accent; border-color: @accent; }
QCalendarWidget QWidget#qt_calendar_navigationbar { background: transparent; }
QCalendarWidget QToolButton {
    color: @text;
    background: transparent;
    border: none;
    border-radius: 6px;
    padding: 3px 6px;
    font-size: 13px;
    font-weight: 600;
}
QCalendarWidget QToolButton:hover { background: @raised; }
QCalendarWidget QToolButton::menu-indicator { image: none; width: 0; }
QCalendarWidget QMenu { font-size: 13px; }
QCalendarWidget QSpinBox {
    background: @window;
    color: @text;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 2px 4px;
    selection-background-color: @accent;
}
QCalendarWidget QTableView {
    background: @surface;
    alternate-background-color: @surface;
    border: none;
    outline: none;
    selection-background-color: transparent;
}
QPushButton[role="link"][compact="true"] { font-size: 12px; padding: 0; }

/* A field whose text isn't what it asks for (a date range's "2024.13") */
QLineEdit[invalid="true"] { border-color: @bad; }
QLabel[role="notice"] { color: @bad; font-size: 12px; }

/* Settings: the sections on the left, a row per setting; preview swatches (board colours,
   piece sets) and round accent colours, outlined in the accent when chosen */
QListWidget#settingsSections { background: transparent; color: @text; outline: none; }
QListWidget#settingsSections::item { padding: 6px 10px; border: none; border-radius: 6px; }
QListWidget#settingsSections::item:hover { background: @raised; }
QListWidget#settingsSections::item:selected { background: @accentTint; color: @accent; }
QScrollArea#settingsScroll, QScrollArea#settingsScroll > QWidget > QWidget {
    background: transparent;
}
QWidget#settingsRow { border-bottom: 1px solid @border; }
QPushButton[role="swatch"] {
    background: transparent;
    border: 2px solid transparent;
    border-radius: 8px;
    padding: 2px;
}
QPushButton[role="swatch"]:hover { border-color: @border; }
QPushButton[role="swatch"]:checked { border-color: @accent; }
QPushButton[role="accentSwatch"] {
    background: transparent;
    border: 2px solid transparent;
    border-radius: 15px;
    padding: 2px;
}
QPushButton[role="accentSwatch"]:checked { border-color: @text; }

/* Sliders (the sounds' volume): a thin track, the part up to the knob in the accent colour */
QSlider::groove:horizontal { height: 4px; background: @border; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal {
    width: 14px;
    height: 14px;
    margin: -5px 0;
    border-radius: 7px;
    background: @text;
}
QSlider::groove:horizontal:disabled, QSlider::sub-page:horizontal:disabled { background: @raised; }

/* Chips: small toggles (the explorer's time controls) */
QPushButton[role="chip"] {
    background: @raised;
    color: @muted;
    border: 1px solid @border;
    border-radius: 12px;
    padding: 3px 10px;
    min-height: 18px;
    font-size: 12px;
}
QPushButton[role="chip"]:hover { color: @text; }
QPushButton[role="chip"]:checked { background: @accentTint; color: @accent; border-color: @accent; }

/* Explorer: the moves and the top games, like the Library's lists; the profile's theory check
   looks the same */
QTreeWidget#explorerMoves, QTreeWidget#explorerGames, QTreeWidget#explorerTablebase,
QTreeWidget#profileTheory {
    background: transparent;
    color: @text;
    outline: none;
}
QTreeWidget#explorerMoves::item, QTreeWidget#explorerGames::item,
QTreeWidget#explorerTablebase::item, QTreeWidget#profileTheory::item {
    padding: 5px 4px;
    border: none;
}
QTreeWidget#explorerMoves QHeaderView, QTreeWidget#explorerTablebase QHeaderView,
QTreeWidget#profileTheory QHeaderView {
    background: transparent;
    border: none;
}
QTreeWidget#explorerMoves QHeaderView::section, QTreeWidget#explorerTablebase QHeaderView::section,
QTreeWidget#profileTheory QHeaderView::section {
    background: transparent;
    color: @muted;
    border: none;
    border-bottom: 1px solid @border;
    /* right: room for the sort arrow, so it never covers a right-aligned title */
    padding: 2px 14px 6px 4px;
    font-size: 10px;
    font-weight: 600;
}
QTreeWidget#explorerMoves::item:hover, QTreeWidget#explorerGames::item:hover,
QTreeWidget#explorerTablebase::item:hover,
QTreeWidget#profileTheory::item:hover {
    background: @raised;
}
QTreeWidget#explorerMoves::item:selected, QTreeWidget#explorerGames::item:selected,
QTreeWidget#explorerTablebase::item:selected,
QTreeWidget#profileTheory::item:selected {
    background: @accentTint;
    color: @accent;
}

/* Databases: the list of databases */
QLabel[role="databaseName"] { color: @text; font-size: 13px; font-weight: 600; }
QListWidget#databaseList, QListWidget#profileList { background: transparent; outline: none; }
QListWidget#databaseList::item, QListWidget#profileList::item { border: none; border-radius: 6px; }
QListWidget#databaseList::item:hover, QListWidget#profileList::item:hover { background: @raised; }
QListWidget#databaseList::item:selected, QListWidget#profileList::item:selected {
    background: @accentTint;
}
QListWidget#fideCandidates { background: transparent; color: @text; outline: none; }
QListWidget#fideCandidates::item { padding: 4px 2px; border: none; border-radius: 6px; }
QListWidget#fideCandidates::item:hover { background: @raised; }

/* Profiles: the account cards, one above the other on the card's own background; each has a
   site badge, ratings with how they moved, and a thin bar for the games downloaded */
QScrollArea#profileAccounts, QWidget#profileAccountsContent { background: transparent; }
QScrollArea#explorerFilterScroll, QWidget#explorerFilter { background: transparent; }
QLabel[role="siteBadge"] {
    background: @window;
    color: @muted;
    border-radius: 4px;
    padding: 2px 6px;
    font-size: 10px;
    font-weight: 700;
}
QLabel[role="ratingValue"] { color: @text; font-weight: 600; }
QLabel[role="ratingChange"] { border-radius: 4px; padding: 0 5px; font-size: 11px; font-weight: 600; }
QLabel[role="ratingChange"][mood="good"] { background: @goodTint; color: @good; }
QLabel[role="ratingChange"][mood="bad"] { background: @badTint; color: @bad; }
QLabel[role="cardFoot"] { color: @dimmed; font-size: 11px; }
QProgressBar#accountProgress { background: @window; border: none; border-radius: 2px; }
QProgressBar#accountProgress::chunk { background: @accent; border-radius: 2px; }

/* Library: a repertoire's numbers in tiles, and the openings in it */
QFrame[statTile="true"] { background: @raised; border: none; border-radius: 8px; }
QLabel[role="statCaption"] { color: @muted; font-size: 11px; }
QLabel[role="statValue"] { color: @text; font-size: 20px; font-weight: 600; }
QTreeWidget#repertoireOpenings, QTreeWidget#repertoireGaps {
    background: transparent;
    color: @text;
    outline: none;
}
QTreeWidget#repertoireOpenings::item, QTreeWidget#repertoireGaps::item {
    padding: 4px 2px;
    border: none;
}
QTreeWidget#repertoireGaps::item:hover { background: @raised; }
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

/* Info tab: label and value rows; a value looks like text until it is hovered or edited */
QLabel[role="infoLabel"] { color: @muted; font-size: 12px; min-width: 64px; }
QLineEdit[role="inlineField"], QComboBox[role="inlineField"] {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 3px 8px;
    min-height: 22px;
}
QLineEdit[role="inlineField"]:hover, QComboBox[role="inlineField"]:hover { background: @raised; }
QLineEdit[role="inlineField"]:focus, QComboBox[role="inlineField"]:focus {
    background: @window;
    border-color: @accent;
}
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
/* The popup's window draws the one rounded box; the list inside it is see-through. */
QComboBoxPrivateContainer {
    background: @surface;
    border: 1px solid @border;
    border-radius: 8px;
}
QComboBox QAbstractItemView {
    background: transparent;
    color: @text;
    border: none;
    padding: 3px;
    outline: none;
    font-size: 13px;
}
QComboBox QAbstractItemView::item {
    min-height: 26px;
    padding: 0 8px;
    border: none;
    border-radius: 6px;
}
QComboBox QAbstractItemView::item:hover,
QComboBox QAbstractItemView::item:selected { background: @accentTint; color: @accent; }

/* Library: file list and games table */
QTreeWidget#libraryFiles {
    background: transparent;
    color: @text;
    outline: none;
    show-decoration-selected: 0; /* the fold arrows are drawn by the tree (LibraryView) */
}
QTreeWidget#libraryFiles::item {
    padding: 4px 4px;
    margin: 1px 0;
    border: none;
    border-radius: 6px; /* rounded like the rows of menus and dropdowns */
}
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
    const std::array<std::pair<const char*, QColor>, 15> names{{
        {"@goodTint", mix(colors.good, colors.raised, 0.18F)}, // before @good, as below
        {"@badTint", mix(colors.bad, colors.raised, 0.18F)},
        {"@good", colors.good},
        {"@bad", colors.bad},
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
    css.replace(QStringLiteral("@checkImage"), checkImage);
    if (textScale != 100) { // every "font-size: 12px", scaled
        static const QRegularExpression size(QStringLiteral(R"(font-size:\s*(\d+)px)"));
        QString scaled;
        qsizetype last = 0;
        auto matches = size.globalMatch(css);
        while (matches.hasNext()) {
            const auto match = matches.next();
            scaled += css.mid(last, match.capturedStart() - last);
            scaled += QStringLiteral("font-size: %1px")
                          .arg(qRound(match.captured(1).toInt() * textScale / 100.0));
            last = match.capturedEnd();
        }
        scaled += css.mid(last);
        css = std::move(scaled);
    }
    return css;
}

} // namespace kiebitz::app
