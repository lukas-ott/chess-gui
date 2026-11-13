#include "LichessExplorer.hpp"

#include "kiebitz/pgn/pgn.hpp"
#include "net/Lichess.hpp"
#include "profiles/LichessLogin.hpp"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QThread>
#include <QUrlQuery>

#include <array>
#include <utility>

namespace kiebitz::app {
namespace {

constexpr qsizetype kCacheSize = 500;

/// Lichess's rating groups: each is from its number up to the next.
constexpr std::array kRatingGroups{0, 1000, 1200, 1400, 1600, 1800, 2000, 2200, 2500};

gamedb::GameResult winner(const QJsonValue& value) {
    if (value.toString() == u"white") {
        return gamedb::GameResult::WhiteWins;
    }
    if (value.toString() == u"black") {
        return gamedb::GameResult::BlackWins;
    }
    return gamedb::GameResult::Draw; // null: a draw
}

} // namespace

LichessExplorer::LichessExplorer(LichessLogin& login, QObject* parent)
    : QObject(parent), m_login(login), m_network(new QNetworkAccessManager(this)) {}

void LichessExplorer::setServers(const QUrl& explorer, const QUrl& lichess) {
    m_explorer = explorer;
    m_lichess = lichess;
    m_cache.clear();
}

QUrl LichessExplorer::url(const QUrl& server, Kind kind, const std::vector<core::Move>& line,
                          const gamedb::ExploreFilter& filter) {
    QUrl result = server.resolved(
        QUrl(kind == Kind::Lichess ? QStringLiteral("/lichess") : QStringLiteral("/masters")));
    QUrlQuery query;
    QStringList played;
    for (const core::Move& move : line) {
        played << QString::fromStdString(move.uci());
    }
    if (kind == Kind::Lichess) {
        query.addQueryItem(QStringLiteral("variant"), QStringLiteral("standard"));
    }
    if (!played.isEmpty()) {
        query.addQueryItem(QStringLiteral("play"), played.join(u','));
    }
    query.addQueryItem(QStringLiteral("moves"), QStringLiteral("20"));
    query.addQueryItem(QStringLiteral("topGames"), QStringLiteral("15"));
    if (kind == Kind::Lichess) {
        query.addQueryItem(QStringLiteral("recentGames"), QStringLiteral("0"));
        // Time controls: none chosen means all.
        QStringList speeds;
        for (const auto& [speed, name] :
             {std::pair{gamedb::Speed::Bullet, "bullet"}, std::pair{gamedb::Speed::Blitz, "blitz"},
              std::pair{gamedb::Speed::Rapid, "rapid"},
              std::pair{gamedb::Speed::Classical, "classical"},
              std::pair{gamedb::Speed::Correspondence, "correspondence"}}) {
            if ((filter.speeds & (1U << static_cast<unsigned>(speed))) != 0) {
                speeds << QString::fromLatin1(name);
            }
        }
        if (!speeds.isEmpty()) {
            query.addQueryItem(QStringLiteral("speeds"), speeds.join(u','));
        }
        // Ratings: every group that overlaps the range asked for.
        if (filter.minRating > 0 || filter.maxRating > 0) {
            QStringList groups;
            for (std::size_t k = 0; k < kRatingGroups.size(); ++k) {
                const int low = kRatingGroups[k];
                const int high = k + 1 < kRatingGroups.size() ? kRatingGroups[k + 1] : 4000;
                if (high > filter.minRating && (filter.maxRating == 0 || low <= filter.maxRating)) {
                    groups << QString::number(low);
                }
            }
            query.addQueryItem(QStringLiteral("ratings"), groups.join(u','));
        }
        if (filter.fromYear > 0) {
            query.addQueryItem(QStringLiteral("since"),
                               QStringLiteral("%1-01").arg(filter.fromYear));
        }
        if (filter.toYear > 0) {
            query.addQueryItem(QStringLiteral("until"), QStringLiteral("%1-12").arg(filter.toYear));
        }
    } else {
        if (filter.fromYear > 0) {
            query.addQueryItem(QStringLiteral("since"), QString::number(filter.fromYear));
        }
        if (filter.toYear > 0) {
            query.addQueryItem(QStringLiteral("until"), QString::number(filter.toYear));
        }
    }
    result.setQuery(query);
    return result;
}

LichessExplorer::Answer LichessExplorer::parse(const QByteArray& json,
                                               const core::Position& position) {
    Answer answer;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const auto count = [](const QJsonObject& object, const char* key) {
        return static_cast<std::uint64_t>(object.value(QLatin1String(key)).toDouble());
    };
    auto& explored = answer.explored;
    explored.white = count(root, "white");
    explored.draws = count(root, "draws");
    explored.black = count(root, "black");
    explored.games = explored.white + explored.draws + explored.black;
    std::uint64_t rated = 0;
    double ratingSum = 0;
    for (const auto& value : root.value(QStringLiteral("moves")).toArray()) {
        const QJsonObject object = value.toObject();
        const auto move =
            position.parseUci(object.value(QStringLiteral("uci")).toString().toStdString());
        if (!move) {
            continue; // not a legal move here: skip it
        }
        gamedb::MoveStats stats;
        stats.move = *move;
        stats.san = position.san(*move);
        stats.white = count(object, "white");
        stats.draws = count(object, "draws");
        stats.black = count(object, "black");
        stats.games = stats.white + stats.draws + stats.black;
        // One average for both players: shown once.
        const int average = object.value(QStringLiteral("averageRating")).toInt();
        stats.whiteElo = average;
        stats.blackElo = average;
        if (average > 0) {
            rated += stats.games;
            ratingSum += static_cast<double>(average) * static_cast<double>(stats.games);
        }
        explored.moves.push_back(std::move(stats));
    }
    if (rated > 0) {
        explored.whiteElo = static_cast<int>(ratingSum / static_cast<double>(rated));
        explored.blackElo = explored.whiteElo;
    }
    for (const auto& value : root.value(QStringLiteral("topGames")).toArray()) {
        const QJsonObject object = value.toObject();
        gamedb::GameInfo game;
        game.id = static_cast<gamedb::GameId>(answer.ids.size());
        const QJsonObject white = object.value(QStringLiteral("white")).toObject();
        const QJsonObject black = object.value(QStringLiteral("black")).toObject();
        game.white = white.value(QStringLiteral("name")).toString().toStdString();
        game.black = black.value(QStringLiteral("name")).toString().toStdString();
        game.whiteElo = white.value(QStringLiteral("rating")).toInt();
        game.blackElo = black.value(QStringLiteral("rating")).toInt();
        game.result = winner(object.value(QStringLiteral("winner")));
        game.date = std::to_string(object.value(QStringLiteral("year")).toInt());
        answer.ids.push_back(object.value(QStringLiteral("id")).toString());
        answer.games.push_back(std::move(game));
    }
    return answer;
}

void LichessExplorer::ask(Kind kind, const std::vector<core::Move>& line,
                          const gamedb::ExploreFilter& filter,
                          std::function<void(const Answer&)> done) {
    if (m_pending) {
        m_pending->abort(); // only the newest position matters
    }
    const QUrl address = url(m_explorer, kind, line, filter);
    const QString key = address.toString();
    if (const auto found = m_cache.constFind(key); found != m_cache.constEnd()) {
        done(*found);
        return;
    }
    core::Position position;
    for (const core::Move& move : line) {
        position.play(move);
    }
    if (lichess::pauseLeftMs() > 0) { // Lichess asked for a minute's break: not yet
        Answer answer;
        answer.error = tr("Lichess asks Kiebitz to slow down. Try again in a minute.");
        done(answer);
        return;
    }
    QNetworkReply* reply =
        m_network->get(lichess::request(address, m_login.token(), lichess::kTimeoutMs));
    m_pending = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, key, position, done = std::move(done)] {
                reply->deleteLater();
                if (reply->error() == QNetworkReply::OperationCanceledError) {
                    return; // asked again meanwhile
                }
                const int status = lichess::status(reply);
                Answer answer;
                if (status == 200) {
                    answer = parse(reply->readAll(), position);
                    if (m_cache.size() >= kCacheSize) {
                        m_cache.clear();
                    }
                    m_cache.insert(key, answer);
                } else if (status == 401) {
                    answer.error = tr("Lichess wants you to sign in again.");
                    m_login.tokenRejected();
                } else if (status == 429) {
                    answer.error = tr("Lichess asks Kiebitz to slow down. Try again in a minute.");
                } else {
                    answer.error = tr("Lichess's explorer isn't answering right now.");
                }
                done(answer);
            });
}

void LichessExplorer::fetchGame(Kind kind, const QString& id,
                                std::function<void(std::optional<core::Game>)> done) {
    const QUrl address = kind == Kind::Masters
                             ? m_explorer.resolved(QUrl(QStringLiteral("/masters/pgn/") + id))
                             : m_lichess.resolved(QUrl(QStringLiteral("/game/export/") + id));
    QNetworkRequest pgnRequest = lichess::request(address, m_login.token(), lichess::kTimeoutMs);
    pgnRequest.setRawHeader("Accept", "application/x-chess-pgn");
    QNetworkReply* reply = m_network->get(pgnRequest);
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done(std::nullopt);
            return;
        }
        auto game = pgn::parseGame(reply->readAll().toStdString());
        done(game.ok() ? std::optional<core::Game>(std::move(*game)) : std::nullopt);
    });
}

repertoire::Explore LichessExplorer::blocking(Kind kind, const gamedb::ExploreFilter& filter) {
    // NOLINTNEXTLINE(bugprone-exception-escape): runs in a worker, like any explorer function
    return [server = m_explorer, kind, filter,
            token = m_login.token()](std::span<const core::Move> line) {
        const std::vector<core::Move> moves(line.begin(), line.end());
        core::Position position;
        for (const core::Move& move : moves) {
            position.play(move);
        }
        // This thread's own network access, waited for right here.
        QNetworkAccessManager network;
        for (int attempt = 0; attempt < 3; ++attempt) {
            // After a 429 (here or anywhere in Kiebitz) Lichess wants a minute's pause.
            QThread::msleep(static_cast<unsigned long>(lichess::pauseLeftMs()));
            QNetworkReply* reply = network.get(
                lichess::request(url(server, kind, moves, filter), token, lichess::kTimeoutMs));
            QEventLoop wait;
            QObject::connect(reply, &QNetworkReply::finished, &wait, &QEventLoop::quit);
            wait.exec();
            reply->deleteLater();
            const int status = lichess::status(reply);
            if (status == 429) {
                continue; // after the pause, once more
            }
            std::vector<repertoire::MoveCount> result;
            if (status == 200) {
                for (const auto& stats : parse(reply->readAll(), position).explored.moves) {
                    result.push_back({stats.move, stats.games});
                }
            }
            return result;
        }
        return std::vector<repertoire::MoveCount>{};
    };
}

} // namespace kiebitz::app
