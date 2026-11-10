#pragma once

#include "AnalysisSource.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QTimer>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace chessgui::app {

/// Evaluations from the Lichess cloud: positions Lichess users have already analysed deeply,
/// fetched over the network (https://lichess.org/api#tag/Analysis). Only common positions are
/// there; for the rest it says so.
///
/// It is gentle with Lichess: one request at a time, only after the board has been still for a
/// moment, every answer remembered, and a full minute's pause if Lichess asks for one.
class CloudEval : public AnalysisSource {
    Q_OBJECT

public:
    explicit CloudEval(QObject* parent = nullptr);

    void analyse(const core::Position& position) override;
    void stop() override;
    void setMultiPv(int lines) override;

    /// Reads a cloud-eval answer for `position` (scores come from White's view; they are turned
    /// into the side to move's, as UCI engines give them). Lines that don't fit the position are
    /// cut where they stop being legal.
    [[nodiscard]] static std::vector<engine::Info> parse(const QByteArray& json,
                                                         const core::Position& position);

private:
    void request();
    void finished(QNetworkReply* reply);
    [[nodiscard]] QString key() const;

    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_reply = nullptr;
    QTimer m_debounce;
    std::optional<core::Position> m_position;
    int m_multiPv = 3;
    QHash<QString, std::vector<engine::Info>> m_cache; ///< by FEN and line count; empty = absent
    QDateTime m_pausedUntil;                           ///< after Lichess said "too many requests"
};

} // namespace chessgui::app
