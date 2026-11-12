#pragma once

#include "kiebitz/core/game.hpp"
#include "kiebitz/gamedb/database.hpp"
#include "kiebitz/repertoire/gaps.hpp"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace kiebitz::app {

class LichessLogin;

/// Lichess's opening explorer (explorer.lichess.ovh): the games played on Lichess, or the masters'
/// over-the-board games, in the same shape as a local database's answer. Lichess asks for a
/// login for it. Answers are remembered for the session, so going back and forth costs nothing.
class LichessExplorer : public QObject {
    Q_OBJECT

public:
    enum class Kind : std::uint8_t { Lichess, Masters };
    struct Answer {
        gamedb::Explored explored;
        std::vector<gamedb::GameInfo> games; ///< top games; `id` is the index into `ids`
        std::vector<QString> ids;            ///< Lichess's game ids
        QString error;                       ///< why there is no answer, or empty
    };

    explicit LichessExplorer(LichessLogin& login, QObject* parent = nullptr);
    /// For tests.
    void setServers(const QUrl& explorer, const QUrl& lichess);

    /// The position after `line` (from the usual start). Only the newest question is answered.
    /// Of the filter, Lichess uses the time controls, ratings and years; the masters the years.
    void ask(Kind kind, const std::vector<core::Move>& line, const gamedb::ExploreFilter& filter,
             std::function<void(const Answer&)> done);
    /// A top game, whole.
    void fetchGame(Kind kind, const QString& id,
                   std::function<void(std::optional<core::Game>)> done);

    /// The explorer for the gap check: asks one position at a time and waits for the answer, so
    /// only for a background thread. Waits a minute when Lichess asks it to slow down.
    [[nodiscard]] repertoire::Explore blocking(Kind kind, const gamedb::ExploreFilter& filter);

    /// The address asked for (tests).
    [[nodiscard]] static QUrl url(const QUrl& server, Kind kind,
                                  const std::vector<core::Move>& line,
                                  const gamedb::ExploreFilter& filter);
    /// Lichess's JSON as an answer, for the position the moves are played from.
    [[nodiscard]] static Answer parse(const QByteArray& json, const core::Position& position);

private:
    LichessLogin& m_login;
    QNetworkAccessManager* m_network = nullptr;
    QUrl m_explorer{QStringLiteral("https://explorer.lichess.ovh")};
    QUrl m_lichess{QStringLiteral("https://lichess.org")};
    QHash<QString, Answer> m_cache; ///< by address
    QPointer<QNetworkReply> m_pending;
};

} // namespace kiebitz::app
