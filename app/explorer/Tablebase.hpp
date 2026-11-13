#pragma once

#include "kiebitz/core/position.hpp"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace kiebitz::app {

/// Lichess's endgame tablebase (Syzygy, up to 7 pieces; DTM where it has it): for a position,
/// who wins with best play and how every move scores. Asked over the network, the newest
/// question only, and remembered.
class Tablebase : public QObject {
    Q_OBJECT

public:
    /// From the point of view of the side the result is for. A cursed win is a win that the
    /// 50-move rule turns into a draw; a blessed loss, a loss it saves.
    enum class Outcome : std::uint8_t {
        Win,
        CursedWin,
        MaybeWin,
        Draw,
        Unknown,
        MaybeLoss,
        BlessedLoss,
        Loss
    };

    /// One move and what it leads to, for the side playing it.
    struct MoveResult {
        core::Move move;
        std::string san;
        Outcome outcome = Outcome::Unknown;
        std::optional<int> dtz; ///< plies to the next capture or pawn move (Syzygy), if known
        std::optional<int> dtm; ///< plies to mate, if known
        bool checkmate = false;
        bool stalemate = false;
    };

    struct Answer {
        Outcome outcome = Outcome::Unknown; ///< for the side to move
        std::optional<int> dtz;
        std::optional<int> dtm;
        bool checkmate = false;
        bool stalemate = false;
        std::vector<MoveResult> moves; ///< best first
        QString error;                 ///< why there is no answer, or empty
    };

    explicit Tablebase(QObject* parent = nullptr);
    /// For tests.
    void setServer(const QUrl& server) { m_server = server; }

    /// The tablebase has the position: 7 pieces or fewer, and no castling rights.
    [[nodiscard]] static bool covers(const core::Position& position);
    /// Asks about a position; `done` gets the answer (straight away when it is known). Only the
    /// newest question is answered.
    void ask(const core::Position& position, std::function<void(const Answer&)> done);

    /// Reads Lichess's answer. (Each move's result there is for the opponent, after the move;
    /// here it is for the side playing it.)
    [[nodiscard]] static Answer parse(const QByteArray& json, const core::Position& position);
    /// "Win", "Draw", ... for the side it is for.
    [[nodiscard]] static QString name(Outcome outcome);
    /// What a cursed win, a blessed loss, ... mean, for a tooltip; empty for the plain ones.
    [[nodiscard]] static QString explanation(Outcome outcome);

private:
    QNetworkAccessManager* m_network = nullptr;
    QUrl m_server{QStringLiteral("https://tablebase.lichess.ovh")};
    QHash<QString, Answer> m_cache; ///< by FEN
    QPointer<QNetworkReply> m_pending;
};

} // namespace kiebitz::app
