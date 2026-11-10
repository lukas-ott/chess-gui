#include "CloudEval.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrlQuery>

namespace kiebitz::app {
namespace {

constexpr int kDebounceMs = 300; // wait until the user stops stepping through moves
constexpr int kTimeoutMs = 10000;

} // namespace

CloudEval::CloudEval(QObject* parent)
    : AnalysisSource(parent), m_network(new QNetworkAccessManager(this)) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &CloudEval::request);
}

void CloudEval::analyse(const core::Position& position) {
    m_position = position;
    if (m_reply != nullptr) {
        m_reply->abort(); // for the old position
    }
    if (const auto cached = m_cache.constFind(key()); cached != m_cache.constEnd()) {
        m_debounce.stop();
        emit updated({position, *cached});
        emit statusChanged(cached->empty() ? tr("Not in the Lichess cloud") : QString());
        return;
    }
    emit updated({position, {}});
    m_debounce.start();
}

void CloudEval::stop() {
    m_position.reset();
    m_debounce.stop();
    if (m_reply != nullptr) {
        m_reply->abort();
    }
}

void CloudEval::setMultiPv(int lines) {
    m_multiPv = lines;
    if (m_position) {
        analyse(*m_position);
    }
}

QString CloudEval::key() const {
    return m_position ? QString::fromStdString(m_position->fen()) + QLatin1Char('|') +
                            QString::number(m_multiPv)
                      : QString();
}

void CloudEval::request() {
    if (!m_position) {
        return;
    }
    if (m_pausedUntil.isValid() && QDateTime::currentDateTimeUtc() < m_pausedUntil) {
        emit statusChanged(tr("Lichess asked for a short break"));
        m_debounce.start(
            static_cast<int>(QDateTime::currentDateTimeUtc().msecsTo(m_pausedUntil) + kDebounceMs));
        return;
    }
    QUrl url(QStringLiteral("https://lichess.org/api/cloud-eval"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("fen"), QString::fromStdString(m_position->fen()));
    query.addQueryItem(QStringLiteral("multiPv"), QString::number(m_multiPv));
    url.setQuery(query);
    QNetworkRequest request(url);
    // A plain client name: no personal details go to Lichess.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Kiebitz/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(kTimeoutMs);
    m_reply = m_network->get(request);
    QNetworkReply* reply = m_reply;
    const core::Position position = *m_position;
    const QString requestKey = key();
    connect(reply, &QNetworkReply::finished, this, [this, reply, position, requestKey] {
        reply->deleteLater();
        if (m_reply == reply) {
            m_reply = nullptr;
        }
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            return; // replaced by a newer position, or stopped
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool current = m_position && requestKey == key();
        if (status == 200) {
            auto lines = parse(reply->readAll(), position);
            m_cache.insert(requestKey, lines);
            if (current) {
                emit updated({position, std::move(lines)});
                emit statusChanged({});
            }
        } else if (status == 404) {
            m_cache.insert(requestKey, {});
            if (current) {
                emit statusChanged(tr("Not in the Lichess cloud"));
            }
        } else if (status == 429) {
            // Lichess's rule for API clients: after a 429, wait a full minute.
            m_pausedUntil = QDateTime::currentDateTimeUtc().addSecs(60);
            if (current) {
                emit statusChanged(tr("Lichess asked for a short break"));
                m_debounce.start();
            }
        } else if (current) {
            emit statusChanged(tr("Couldn't reach Lichess"));
        }
    });
}

std::vector<engine::Info> CloudEval::parse(const QByteArray& json, const core::Position& position) {
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const int depth = root.value(QStringLiteral("depth")).toInt();
    const auto nodes =
        static_cast<std::uint64_t>(root.value(QStringLiteral("knodes")).toInteger() * 1000);
    std::vector<engine::Info> lines;
    int multiPv = 0;
    const QJsonArray pvs = root.value(QStringLiteral("pvs")).toArray();
    for (const auto& value : pvs) {
        const QJsonObject pv = value.toObject();
        engine::Info info;
        info.multiPv = ++multiPv;
        info.depth = depth;
        info.nodes = nodes;
        if (pv.contains(QStringLiteral("mate"))) {
            info.score = {engine::Score::Kind::Mate, pv.value(QStringLiteral("mate")).toInt()};
        } else {
            info.score = {engine::Score::Kind::Centipawns, pv.value(QStringLiteral("cp")).toInt()};
        }
        // White's view → the side to move's (what UCI engines report).
        info.score = info.score.forWhite(position.sideToMove());
        for (const QString& move : pv.value(QStringLiteral("moves"))
                                       .toString()
                                       .split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            info.pv.push_back(move.toStdString());
        }
        // Keep the moves that are legal here (castling comes as king-takes-rook; parseUci
        // understands that), so the line can be shown and played.
        const auto moves = engine::pvMoves(position, info.pv);
        info.pv.resize(moves.size());
        for (std::size_t i = 0; i < moves.size(); ++i) {
            info.pv[i] = moves[i].uci();
        }
        if (!info.pv.empty()) {
            lines.push_back(std::move(info));
        }
    }
    return lines;
}

} // namespace kiebitz::app
