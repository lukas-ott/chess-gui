#include "Fide.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace kiebitz::app {
namespace {

/// The parts of a name, without case, accents or punctuation: "Ott, Lukas" → {"ott", "lukas"}.
QSet<QString> parts(const QString& name) {
    QString plain;
    for (const QChar c : name.normalized(QString::NormalizationForm_D)) {
        if (c.isLetter() || c.isDigit()) {
            plain += c.toLower();
        } else if (c.isSpace() || c == u',' || c == u'-' || c == u'.') {
            plain += u' ';
        } // combining accents and the rest are dropped
    }
    const QStringList words = plain.split(u' ', Qt::SkipEmptyParts);
    return {words.begin(), words.end()};
}

} // namespace

QString FidePlayer::profileUrl() const {
    // As Lichess makes the name part ("Carlsen, Magnus" → "Carlsen_Magnus"): accents off,
    // spaces as "_", anything but ASCII letters, digits, "_" and "-" left out (Lichess's regex
    // has only those as word characters, so "ø" goes). Another name part would only redirect.
    QString slug;
    bool space = false;
    for (const QChar c : name.normalized(QString::NormalizationForm_D)) {
        if (c.isSpace()) {
            space = !slug.isEmpty();
            continue;
        }
        if (c.category() == QChar::Mark_NonSpacing) {
            continue; // an accent, split off its letter
        }
        const char16_t code = c.unicode();
        const bool ascii = (code >= u'a' && code <= u'z') || (code >= u'A' && code <= u'Z') ||
                           (code >= u'0' && code <= u'9') || code == u'_' || code == u'-';
        if (!ascii) {
            continue;
        }
        if (space) {
            slug += u'_';
            space = false;
        }
        slug += c;
    }
    return QStringLiteral("https://lichess.org/fide/%1/%2").arg(id).arg(slug);
}

std::vector<FidePlayer> parseFideSearch(const QByteArray& json) {
    std::vector<FidePlayer> players;
    for (const auto& value : QJsonDocument::fromJson(json).array()) {
        const QJsonObject object = value.toObject();
        FidePlayer player;
        player.id = object.value(QStringLiteral("id")).toInt();
        player.name = object.value(QStringLiteral("name")).toString();
        if (player.id <= 0 || player.name.isEmpty()) {
            continue;
        }
        player.title = object.value(QStringLiteral("title")).toString();
        player.federation = object.value(QStringLiteral("federation")).toString();
        player.standard = object.value(QStringLiteral("standard")).toInt();
        player.rapid = object.value(QStringLiteral("rapid")).toInt();
        player.blitz = object.value(QStringLiteral("blitz")).toInt();
        player.born = object.value(QStringLiteral("year")).toInt();
        players.push_back(player);
    }
    return players;
}

std::vector<FidePlayer> rankFideMatches(std::vector<FidePlayer> players, const QString& name) {
    const QSet<QString> wanted = parts(name);
    const auto fit = [&](const FidePlayer& player) {
        const QSet<QString> have = parts(player.name);
        const auto shared = static_cast<int>(QSet<QString>(wanted).intersect(have).size());
        const bool all = shared == wanted.size() && shared == have.size();
        return std::pair{all ? 1 : 0, shared};
    };
    std::erase_if(players, [&](const FidePlayer& player) { return fit(player).second == 0; });
    const auto best = [](const FidePlayer& player) {
        return std::max({player.standard, player.rapid, player.blitz});
    };
    std::stable_sort(players.begin(), players.end(), [&](const auto& a, const auto& b) {
        const auto fa = fit(a);
        const auto fb = fit(b);
        return fa != fb ? fa > fb : best(a) > best(b);
    });
    return players;
}

} // namespace kiebitz::app
