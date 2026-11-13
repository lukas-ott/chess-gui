#pragma once

#include <QObject>
#include <QString>
#include <QVariant>

namespace kiebitz::app {

/// The app's settings, each under its key (as QSettings keeps them) with its default, and a
/// signal when one changes, so the Settings page, the menus and what they change stay in step.
/// One for the app (instance()).
class Settings : public QObject {
    Q_OBJECT

public:
    // Appearance
    static constexpr auto kAppearance = "app/appearance"; ///< "system", "light", "dark"
    static constexpr auto kAccent = "app/accent";         ///< "#rrggbb", or "" for the default
    static constexpr auto kTextSize = "app/textSize";     ///< percent: 90, 100, 110, 120
    static constexpr auto kLanguage = "app/language";     ///< "system", "en", "de", "nb"
    // Board
    static constexpr auto kTheme = "board/theme";
    static constexpr auto kPieces = "board/pieces";
    static constexpr auto kCoordinates = "board/coordinates"; ///< "none", "edge", "all"
    static constexpr auto kAnimation = "board/animation";     ///< "off", "fast", "normal", "slow"
    static constexpr auto kLegalDots = "board/legalDots";     ///< dots for the legal moves
    static constexpr auto kLastMove = "board/lastMove";       ///< the last move highlighted
    static constexpr auto kMoveMethod = "board/moveMethod";   ///< "both", "drag", "click"
    static constexpr auto kAutoQueen = "board/autoQueen";     ///< promote without asking
    static constexpr auto kWheel = "board/wheel";             ///< the wheel steps through moves
    // Moves
    static constexpr auto kNotationRows = "notation/rows";   ///< the move list in rows
    static constexpr auto kFigurines = "notation/figurines"; ///< ♘f3 instead of Nf3
    // Repertoire
    static constexpr auto kPracticeDepth = "practice/depth";
    static constexpr auto kGapShare = "gaps/minShare"; ///< percent of the games in a position
    static constexpr auto kGapGames = "gaps/minGames"; ///< games in a position to judge it
    // Sounds
    static constexpr auto kSounds = "sound/on";
    static constexpr auto kVolume = "sound/volume"; ///< percent
    static constexpr auto kSoundSet = "sound/set";  ///< "standard", "woodland", "sfx", ... (Sounds)

    static Settings& instance();

    /// The value, or its default when it was never set.
    // Members, not static: everyone goes through instance(), which also sends changed().
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    [[nodiscard]] QVariant value(const char* key) const;
    [[nodiscard]] QString string(const char* key) const { return value(key).toString(); }
    [[nodiscard]] bool flag(const char* key) const { return value(key).toBool(); }
    [[nodiscard]] int number(const char* key) const { return value(key).toInt(); }
    /// Sets it (and says so, if it is a change).
    void setValue(const char* key, const QVariant& value);
    /// Back to the default (and says so).
    void reset(const char* key);

    /// How long the last move slides, by the animation setting (0: it doesn't).
    [[nodiscard]] int animationMs() const;

signals:
    void changed(const QString& key);

private:
    Settings() = default;
};

} // namespace kiebitz::app
