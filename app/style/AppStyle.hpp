#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <cstdint>
#include <memory>

class QApplication;

namespace kiebitz::app {

/// The app's look: a flat, modern palette with rounded cards (inspired by Mantine, the design
/// system En Croissant uses), in a dark and a light variant.
///
/// It uses Qt's Fusion style plus one style sheet, so every widget is drawn the same way on
/// every platform. The menu bar stays native on macOS.
class AppStyle : public QObject {
    Q_OBJECT

public:
    enum class Appearance : std::uint8_t { System, Light, Dark };

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

    [[nodiscard]] static Colors darkColors();
    [[nodiscard]] static Colors lightColors();

    explicit AppStyle(QApplication& app);

    /// Applies an appearance now and follows the system setting from then on if it's System.
    void setAppearance(Appearance appearance);
    /// The accent colour (buttons, selections, highlights); invalid: the theme's own.
    void setAccent(const QColor& accent);
    [[nodiscard]] QColor accentOverride() const { return m_accent; }
    /// The size of the app's text, in percent of the usual (90 to 130).
    void setTextScale(int percent);
    [[nodiscard]] int textScale() const noexcept { return m_textScale; }
    /// Makes tooltips see-through, so their rounded corners show (installed on the app).
    bool eventFilter(QObject* watched, QEvent* event) override;
    [[nodiscard]] Appearance appearance() const noexcept { return m_appearance; }
    [[nodiscard]] bool isDark() const noexcept { return m_dark; }
    [[nodiscard]] const Colors& colors() const noexcept { return m_colors; }

    [[nodiscard]] static QString toString(Appearance appearance);
    [[nodiscard]] static Appearance fromString(const QString& text);

signals:
    /// The colours changed (appearance switched, or the system went light/dark).
    void changed();

private:
    void apply();
    /// `checkImage`: a PNG of a tick in the accent colour (for checked menu items).
    [[nodiscard]] static QString styleSheet(const Colors& colors, const QString& checkImage = {},
                                            int textScale = 100);
    /// Draws the tick for the style sheet; returns its path.
    [[nodiscard]] QString writeCheckImage(const QColor& color);

    QApplication& m_app;
    Appearance m_appearance = Appearance::System;
    bool m_dark = true;
    Colors m_colors = darkColors();
    QColor m_accent;       ///< invalid: the theme's
    int m_textScale = 100; ///< percent
    qreal m_baseFont = 0;  ///< the system's text size (points), before scaling
    bool m_applying = false;
    std::unique_ptr<QTemporaryDir> m_images; ///< images the style sheet uses
};

} // namespace kiebitz::app
