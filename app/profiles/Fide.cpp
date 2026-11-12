#include "Fide.hpp"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>

namespace kiebitz::app {
namespace {

int number(const QString& text) {
    return text.trimmed().toInt();
}

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

std::vector<FidePlayer> parseFideSearch(const QString& html) {
    std::vector<FidePlayer> players;
    static const QRegularExpression row(QStringLiteral("<tr>(.*?)</tr>"),
                                        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression id(QStringLiteral(R"(class="box-div">\s*(\d+)\s*<)"));
    static const QRegularExpression name(QStringLiteral(R"(class="found_name">([^<]*)<)"));
    static const QRegularExpression title(QStringLiteral(R"(data-label="title">([^<]*)<)"));
    static const QRegularExpression federation(QStringLiteral(R"re(alt="([A-Z]{3})")re"));
    static const QRegularExpression rating(QStringLiteral(R"(data-label="Rtg">\s*(\d*)\s*<)"));
    static const QRegularExpression born(QStringLiteral(R"(data-label="B-Year">\s*(\d*)\s*<)"));
    auto rows = row.globalMatch(html);
    while (rows.hasNext()) {
        const QString cells = rows.next().captured(1);
        const auto idMatch = id.match(cells);
        const auto nameMatch = name.match(cells);
        if (!idMatch.hasMatch() || !nameMatch.hasMatch()) {
            continue; // the header row
        }
        FidePlayer player;
        player.id = number(idMatch.captured(1));
        player.name = nameMatch.captured(1).trimmed();
        player.title = title.match(cells).captured(1).trimmed();
        player.federation = federation.match(cells).captured(1);
        auto ratings = rating.globalMatch(cells);
        const std::array<int*, 3> fields{&player.standard, &player.rapid, &player.blitz};
        for (int* field : fields) {
            if (ratings.hasNext()) {
                *field = number(ratings.next().captured(1));
            }
        }
        player.born = number(born.match(cells).captured(1));
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
