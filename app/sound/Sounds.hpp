#pragma once

#include "kiebitz/core/game.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>

namespace kiebitz::app {

/// The board's sounds: a move, a capture, a check, from the sound set chosen (Lichess's sets by
/// Enigmahack, in the resources). Played as macOS sound effects (AudioToolbox's system
/// sounds): no audio stream is opened or kept, no output device held, nothing about other apps'
/// sound or the system volume is touched; macOS mixes them in on the device it plays sound
/// effects on. The Settings' volume makes the set's samples softer or louder, as WAV files
/// written for it.
class Sounds : public QObject {
    Q_OBJECT

public:
    enum class Kind : std::uint8_t { Move, Capture, Check };

    explicit Sounds(QObject* parent = nullptr);
    ~Sounds() override;
    Sounds(const Sounds&) = delete;
    Sounds& operator=(const Sounds&) = delete;
    Sounds(Sounds&&) = delete;
    Sounds& operator=(Sounds&&) = delete;

    /// Plays one, if sounds are on (Settings). Several within a moment (stepping through a game
    /// with the arrow keys): only the first.
    void play(Kind kind);
    /// The sound for a move of a game just shown: a capture, a check, or a plain move.
    void playMove(const core::Game& game, core::NodeId id);

    /// A set's sound as a WAV file (16-bit mono), at a volume in percent: for a check, the move's
    /// if the set has none; empty for a set there isn't.
    [[nodiscard]] static QByteArray wav(const QString& set, Kind kind, int volume);
    /// The sets there are, by name ("sfx", ...), the first being the default.
    [[nodiscard]] static QStringList sets();

private:
    void release();

    QString m_folder;                     ///< where the WAV files are written
    int m_volume = -1;                    ///< the volume the system sounds were made at
    QString m_set;                        ///< ... and the set
    std::array<std::uint32_t, 3> m_ids{}; ///< macOS's SystemSoundIDs (0: none)
    qint64 m_last = 0;                    ///< when the last one played (ms)
};

} // namespace kiebitz::app
