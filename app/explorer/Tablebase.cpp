#include "Tablebase.hpp"

#include "net/Lichess.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrlQuery>

#include <utility>

namespace kiebitz::app {
namespace {

using Outcome = Tablebase::Outcome;

constexpr qsizetype kCacheSize = 500;

Outcome outcomeOf(const QString& category) {
    if (category == u"win" || category == u"syzygy-win") {
        return Outcome::Win;
    }
    if (category == u"cursed-win") {
        return Outcome::CursedWin;
    }
    if (category == u"maybe-win") {
        return Outcome::MaybeWin;
    }
    if (category == u"draw") {
        return Outcome::Draw;
    }
    if (category == u"maybe-loss") {
        return Outcome::MaybeLoss;
    }
    if (category == u"blessed-loss") {
        return Outcome::BlessedLoss;
    }
    if (category == u"loss" || category == u"syzygy-loss") {
        return Outcome::Loss;
    }
    return Outcome::Unknown;
}

/// The same result seen from the other side.
Outcome flipped(Outcome outcome) {
    switch (outcome) {
    case Outcome::Win:
        return Outcome::Loss;
    case Outcome::CursedWin:
        return Outcome::BlessedLoss;
    case Outcome::MaybeWin:
        return Outcome::MaybeLoss;
    case Outcome::MaybeLoss:
        return Outcome::MaybeWin;
    case Outcome::BlessedLoss:
        return Outcome::CursedWin;
    case Outcome::Loss:
        return Outcome::Win;
    case Outcome::Draw:
    case Outcome::Unknown:
        break;
    }
    return outcome;
}

/// A distance, made positive (Lichess signs it by who wins); none for null.
std::optional<int> distance(const QJsonValue& value) {
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const int plies = value.toInt();
    return plies < 0 ? -plies : plies;
}

} // namespace

Tablebase::Tablebase(QObject* parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

bool Tablebase::covers(const core::Position& position) {
    return position.pieceCount() <= 7 && position.castlingRights() == core::CastlingRights{};
}

void Tablebase::ask(const core::Position& position, std::function<void(const Answer&)> done) {
    if (m_pending) {
        m_pending->abort(); // the board has moved on
    }
    const QString fen = QString::fromStdString(position.fen());
    if (const auto found = m_cache.constFind(fen); found != m_cache.constEnd()) {
        done(*found);
        return;
    }
    if (lichess::pauseLeftMs() > 0) { // Lichess asked for a minute's break
        Answer answer;
        answer.error = tr("Lichess asks Kiebitz to slow down. Try again in a minute.");
        done(answer);
        return;
    }
    QUrl url = m_server.resolved(QUrl(QStringLiteral("/standard")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("fen"), fen);
    url.setQuery(query);
    QNetworkReply* reply = m_network->get(lichess::request(url, {}, lichess::kTimeoutMs));
    m_pending = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, fen, position, done = std::move(done)] {
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
                    m_cache.insert(fen, answer);
                } else if (status == 429) {
                    answer.error = tr("Lichess asks Kiebitz to slow down. Try again in a minute.");
                } else {
                    answer.error = tr("The tablebase isn't answering right now.");
                }
                done(answer);
            });
}

Tablebase::Answer Tablebase::parse(const QByteArray& json, const core::Position& position) {
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    Answer answer;
    if (root.isEmpty()) {
        answer.error = tr("The tablebase's answer couldn't be read.");
        return answer;
    }
    answer.outcome = outcomeOf(root.value(QStringLiteral("category")).toString());
    answer.dtz = distance(root.value(QStringLiteral("dtz")));
    answer.dtm = distance(root.value(QStringLiteral("dtm")));
    answer.checkmate = root.value(QStringLiteral("checkmate")).toBool();
    answer.stalemate = root.value(QStringLiteral("stalemate")).toBool();
    const QJsonArray moves = root.value(QStringLiteral("moves")).toArray();
    for (const auto& value : moves) {
        const QJsonObject object = value.toObject();
        const auto move =
            position.parseUci(object.value(QStringLiteral("uci")).toString().toStdString());
        if (!move) {
            continue; // not a legal move here: nothing to show
        }
        MoveResult result;
        result.move = *move;
        result.san = object.value(QStringLiteral("san")).toString().toStdString();
        if (result.san.empty()) {
            result.san = position.san(*move);
        }
        // Lichess gives the result for the side to move after the move: the opponent.
        result.outcome = flipped(outcomeOf(object.value(QStringLiteral("category")).toString()));
        result.dtz = distance(object.value(QStringLiteral("dtz")));
        result.dtm = distance(object.value(QStringLiteral("dtm")));
        result.checkmate = object.value(QStringLiteral("checkmate")).toBool();
        result.stalemate = object.value(QStringLiteral("stalemate")).toBool();
        if (result.checkmate) {
            result.outcome = Outcome::Win;
        }
        answer.moves.push_back(std::move(result));
    }
    return answer;
}

QString Tablebase::name(Outcome outcome) {
    switch (outcome) {
    case Outcome::Win:
        return tr("Win");
    case Outcome::CursedWin:
        return tr("Cursed win");
    case Outcome::MaybeWin:
        return tr("Win?");
    case Outcome::Draw:
        return tr("Draw");
    case Outcome::MaybeLoss:
        return tr("Loss?");
    case Outcome::BlessedLoss:
        return tr("Blessed loss");
    case Outcome::Loss:
        return tr("Loss");
    case Outcome::Unknown:
        break;
    }
    return tr("Unknown");
}

QString Tablebase::explanation(Outcome outcome) {
    switch (outcome) {
    case Outcome::CursedWin:
        return tr("A win, but the 50-move rule makes it a draw");
    case Outcome::BlessedLoss:
        return tr("A loss, but the 50-move rule saves it as a draw");
    case Outcome::MaybeWin:
    case Outcome::MaybeLoss:
        return tr("Depends on the 50-move rule: the tablebase can't say for sure");
    case Outcome::Win:
    case Outcome::Draw:
    case Outcome::Loss:
    case Outcome::Unknown:
        break;
    }
    return {};
}

} // namespace kiebitz::app
