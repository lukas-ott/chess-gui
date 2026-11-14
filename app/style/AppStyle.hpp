#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <memory>
#include <vector>

class QApplication;

namespace kiebitz::app {

/// The app's look: a flat, modern palette with rounded cards (inspired by Mantine, the design
/// system En Croissant uses), in dark and light themes.
///
/// It uses Qt's Fusion style plus one style sheet, so every widget is drawn the same way on
/// every platform. The menu bar stays native on macOS.
class AppStyle : public QObject {
    Q_OBJECT

public:
    struct Colors {
        QColor window;  ///< background behind everything
        QColor surface; ///< cards and panels
        QColor raised;  ///< hovered buttons and tabs
        QColor border;
        QColor text;
        QColor muted;  ///< secondary text
        QColor dimmed; ///< disabled text
        QColor accent;
        QColor accentText; ///< text on accent-coloured backgrounds
        QColor good;       ///< right (a correct answer in practice)
        QColor bad;        ///< wrong (a mistake in practice)
        QColor whiteWins;  ///< result bars: White won, drawn, Black won
        QColor draws;
        QColor blackWins;
    };

    /// The default dark and light colours (Graphite and Daylight).
    [[nodiscard]] static Colors darkColors();
    [[nodiscard]] static Colors lightColors();

    /// A named set of colours, dark or light. The app shows one; whether it is dark or light
    /// (macOS's own parts too: title bar, menus, dialogs) follows from it.
    struct Theme {
        QString id;   ///< stored in the settings
        QString name; ///< shown
        bool dark = true;
        Colors colors;
    };
    /// Every theme, the dark ones first; each kind's first is its default.
    [[nodiscard]] static const std::vector<Theme>& themes();
    /// The theme with this id; nullptr if there is none.
    [[nodiscard]] static const Theme* find(const QString& id);

    explicit AppStyle(QApplication& app);

    /// Shows a theme (its id). "" or an unknown id: Graphite or Daylight, as macOS is set, and
    /// following it when that changes.
    void setTheme(const QString& id);
    /// The theme shown now (for "", the one macOS's setting picked).
    [[nodiscard]] const Theme& theme() const noexcept { return *m_theme; }
    /// The accent colour (buttons, selections, highlights); invalid: the theme's own.
    void setAccent(const QColor& accent);
    [[nodiscard]] QColor accentOverride() const { return m_accent; }
    /// The size of the app's text, in percent of the usual (90 to 130).
    void setTextScale(int percent);
    [[nodiscard]] int textScale() const noexcept { return m_textScale; }
    /// Makes tooltips see-through, so their rounded corners show (installed on the app).
    bool eventFilter(QObject* watched, QEvent* event) override;
    [[nodiscard]] bool isDark() const noexcept { return m_dark; }
    [[nodiscard]] const Colors& colors() const noexcept { return m_colors; }

signals:
    /// The colours changed (another theme, or macOS went light/dark while following it).
    void changed();

private:
    void apply();
    /// `checkImage`: a PNG of a tick in the accent colour (for checked menu items).
    [[nodiscard]] static QString styleSheet(const Colors& colors, const QString& checkImage = {},
                                            int textScale = 100);
    /// Draws the tick for the style sheet; returns its path.
    [[nodiscard]] QString writeCheckImage(const QColor& color);

    QApplication& m_app;
    QString m_themeId;              ///< as set: "" follows macOS
    const Theme* m_theme = nullptr; ///< shown now
    bool m_dark = true;
    Colors m_colors = darkColors();
    QColor m_accent;       ///< invalid: the theme's
    int m_textScale = 100; ///< percent
    qreal m_baseFont = 0;  ///< the system's text size (points), before scaling
    bool m_applying = false;
    std::unique_ptr<QTemporaryDir> m_images; ///< images the style sheet uses
};

} // namespace kiebitz::app
