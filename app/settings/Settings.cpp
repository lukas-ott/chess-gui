#include "Settings.hpp"

#include <QSettings>

#include <map>
#include <string_view>

namespace kiebitz::app {
namespace {

/// The defaults. Keys from before keep the defaults they had.
const std::map<std::string_view, QVariant>& defaults() {
    static const std::map<std::string_view, QVariant> values{
        {Settings::kLanguage, QStringLiteral("system")},
        {Settings::kAppTheme, QString()},
        {Settings::kAccent, QString()},
        {Settings::kTextSize, 100},
        {Settings::kTheme, QString()},
        {Settings::kPieces, QString()},
        {Settings::kCoordinates, QStringLiteral("edge")},
        {Settings::kAnimation, QStringLiteral("normal")},
        {Settings::kLegalDots, true},
        {Settings::kLastMove, true},
        {Settings::kMoveMethod, QStringLiteral("both")},
        {Settings::kAutoQueen, false},
        {Settings::kWheel, true},
        {Settings::kNotationRows, false},
        {Settings::kFigurines, false},
        {Settings::kPracticeDepth, 15},
        {Settings::kGapShare, 5},
        {Settings::kGapGames, 20},
        {Settings::kSounds, true},
        {Settings::kVolume, 70},
        {Settings::kSoundSet, QStringLiteral("standard")},
    };
    return values;
}

} // namespace

Settings& Settings::instance() {
    static Settings settings;
    return settings;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): see the header
QVariant Settings::value(const char* key) const {
    const QSettings stored;
    const QString name = QString::fromLatin1(key);
    if (stored.contains(name)) {
        QVariant value = stored.value(name);
        // Coordinates were on or off before they could be on every square.
        if (std::string_view(key) == kCoordinates &&
            (value.toString() == u"true" || value.toString() == u"false")) {
            return value.toBool() ? QStringLiteral("edge") : QStringLiteral("none");
        }
        return value;
    }
    const auto found = defaults().find(key);
    return found != defaults().end() ? found->second : QVariant();
}

void Settings::setValue(const char* key, const QVariant& value) {
    const QVariant before = this->value(key);
    QSettings().setValue(QString::fromLatin1(key), value);
    if (before != value) {
        emit changed(QString::fromLatin1(key));
    }
}

void Settings::reset(const char* key) {
    const QVariant before = value(key);
    QSettings().remove(QString::fromLatin1(key));
    if (before != value(key)) {
        emit changed(QString::fromLatin1(key));
    }
}

int Settings::animationMs() const {
    const QString speed = string(kAnimation);
    if (speed == u"off") {
        return 0;
    }
    if (speed == u"fast") {
        return 90;
    }
    if (speed == u"slow") {
        return 280;
    }
    return 160; // normal: as it always was
}

} // namespace kiebitz::app
