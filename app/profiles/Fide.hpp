#pragma once

#include <QString>

#include <vector>

namespace kiebitz::app {

/// A player in FIDE's rating list.
struct FidePlayer {
    int id = 0;
    QString name;       ///< as FIDE writes it: "Carlsen, Magnus"
    QString title;      ///< "GM", "IM", ... or empty
    QString federation; ///< "NOR"
    int standard = 0;
    int rapid = 0;
    int blitz = 0;
    int born = 0;

    [[nodiscard]] QString profileUrl() const {
        return QStringLiteral("https://ratings.fide.com/profile/%1").arg(id);
    }
    friend bool operator==(const FidePlayer&, const FidePlayer&) = default;
};

/// The players in the answer of FIDE's rating search (ratings.fide.com, an HTML table).
[[nodiscard]] std::vector<FidePlayer> parseFideSearch(const QString& html);

/// The players sorted by how well their names fit `name` (any order of first and last name,
/// any case and accents): those with every part of the name first, then the most parts; among
/// equals, the higher rated. Players that share no part of the name are left out.
[[nodiscard]] std::vector<FidePlayer> rankFideMatches(std::vector<FidePlayer> players,
                                                      const QString& name);

} // namespace kiebitz::app
