#pragma once

#include <QByteArray>
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

    /// Lichess's page for the player (FIDE's own site, ratings.fide.com, is often away):
    /// lichess.org/fide/<id>/<name as Lichess writes it in addresses>.
    [[nodiscard]] QString profileUrl() const;
    friend bool operator==(const FidePlayer&, const FidePlayer&) = default;
};

/// The players in the answer of Lichess's search of FIDE's rating list
/// (lichess.org/api/fide/player?q=..., JSON; Lichess keeps a copy of the list).
[[nodiscard]] std::vector<FidePlayer> parseFideSearch(const QByteArray& json);

/// The players sorted by how well their names fit `name` (any order of first and last name,
/// any case and accents): those with every part of the name first, then the most parts; among
/// equals, the higher rated. Players that share no part of the name are left out.
[[nodiscard]] std::vector<FidePlayer> rankFideMatches(std::vector<FidePlayer> players,
                                                      const QString& name);

} // namespace kiebitz::app
