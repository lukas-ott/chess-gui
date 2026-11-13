#include "Sounds.hpp"

#include "settings/Settings.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace kiebitz::app {
namespace {

constexpr qint64 kGapMs = 60; ///< sounds closer together than this: only the first

QString fileName(Sounds::Kind kind) {
    switch (kind) {
    case Sounds::Kind::Capture:
        return QStringLiteral("capture");
    case Sounds::Kind::Check:
        return QStringLiteral("check");
    case Sounds::Kind::Move:
        break;
    }
    return QStringLiteral("move");
}

std::uint32_t u32At(const QByteArray& data, qsizetype at) {
    std::uint32_t value = 0;
    for (int k = 3; k >= 0; --k) {
        value = (value << 8U) | static_cast<std::uint8_t>(data[at + k]);
    }
    return value;
}

} // namespace

QStringList Sounds::sets() {
    return {QStringLiteral("standard"), QStringLiteral("woodland"), QStringLiteral("sfx"),
            QStringLiteral("piano"),    QStringLiteral("nes"),      QStringLiteral("futuristic")};
}

QByteArray Sounds::wav(const QString& set, Kind kind, int volume) {
    QFile resource(QStringLiteral(":/sounds/%1/%2.wav").arg(set, fileName(kind)));
    if (!resource.open(QIODevice::ReadOnly)) {
        // A set without a check sound (Lichess's standard has none): its move sound.
        return kind == Kind::Check ? wav(set, Kind::Move, volume) : QByteArray();
    }
    const QByteArray original = resource.readAll();
    if (original.size() < 12 || !original.startsWith("RIFF") || original.mid(8, 4) != "WAVE") {
        return {};
    }
    // Its chunks: "fmt " (16-bit PCM expected) and "data"; others (e.g. padding) are skipped.
    QByteArray format;
    QByteArray samples;
    for (qsizetype at = 12; at + 8 <= original.size();) {
        const QByteArray id = original.mid(at, 4);
        const auto length = static_cast<qsizetype>(u32At(original, at + 4));
        if (id == "fmt ") {
            format = original.mid(at + 8, length);
        } else if (id == "data") {
            samples = original.mid(at + 8, length);
        }
        at += 8 + length + (length % 2); // chunks are padded to an even length
    }
    if (format.size() < 16 || static_cast<std::uint8_t>(format[14]) != 16) {
        return {}; // not 16-bit
    }
    // Softer or louder: every sample times the volume.
    const double gain = std::clamp(volume, 0, 100) / 100.0;
    for (qsizetype i = 0; i + 1 < samples.size(); i += 2) {
        const auto sample = static_cast<std::int16_t>(
            static_cast<std::uint32_t>(static_cast<std::uint8_t>(samples[i])) |
            (static_cast<std::uint32_t>(static_cast<std::uint8_t>(samples[i + 1])) << 8U));
        const auto scaled = static_cast<std::int16_t>(
            std::clamp(static_cast<double>(sample) * gain, -32768.0, 32767.0));
        const auto bits = static_cast<std::uint32_t>(static_cast<std::uint16_t>(scaled));
        samples[i] = static_cast<char>(bits & 0xFFU);
        samples[i + 1] = static_cast<char>((bits >> 8U) & 0xFFU);
    }
    // A plain WAV: RIFF, the format as it was, the samples.
    const auto u32 = [](QByteArray& out, std::uint32_t value) {
        for (unsigned k = 0; k < 4; ++k) {
            out.append(static_cast<char>((value >> (8U * k)) & 0xFFU));
        }
    };
    QByteArray out("RIFF");
    u32(out, static_cast<std::uint32_t>(4 + 8 + format.size() + 8 + samples.size()));
    out.append("WAVEfmt ");
    u32(out, static_cast<std::uint32_t>(format.size()));
    out.append(format);
    out.append("data");
    u32(out, static_cast<std::uint32_t>(samples.size()));
    out.append(samples);
    return out;
}

Sounds::Sounds(QObject* parent) : QObject(parent) {
    QString folder = qEnvironmentVariable("KIEBITZ_SETTINGS_DIR"); // tests: never the real one
    if (folder.isEmpty()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    }
    m_folder = QDir(folder).filePath(QStringLiteral("sounds"));
    // A new volume: new files (made when next played).
    connect(&Settings::instance(), &Settings::changed, this, [this](const QString& key) {
        if (key == QLatin1String(Settings::kVolume) || key == QLatin1String(Settings::kSoundSet)) {
            release();
        }
    });
}

Sounds::~Sounds() {
    release();
}

void Sounds::release() {
#ifdef __APPLE__
    for (std::uint32_t& id : m_ids) {
        if (id != 0) {
            AudioServicesDisposeSystemSoundID(id);
            id = 0;
        }
    }
#endif
    m_volume = -1;
}

void Sounds::playMove(const core::Game& game, core::NodeId id) {
    if (id == core::Game::root()) {
        return;
    }
    const core::Move move = game.node(id).move;
    const core::Position before = game.positionAt(game.parent(id));
    // A capture: a piece on the target square, or a pawn moving aside to an empty one.
    const core::Piece moved = before.pieceAt(move.from);
    const bool captures =
        !before.pieceAt(move.to).isNone() ||
        (moved.type == core::PieceType::Pawn && move.from.file() != move.to.file());
    Kind kind = captures ? Kind::Capture : Kind::Move;
    if (game.positionAt(id).inCheck()) {
        kind = Kind::Check;
    }
    play(kind);
}

void Sounds::play(Kind kind) {
    const Settings& settings = Settings::instance();
    const int volume = settings.number(Settings::kVolume);
    if (!settings.flag(Settings::kSounds) || volume <= 0) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_last < kGapMs) {
        return; // stepping fast: no clatter
    }
    m_last = now;
#ifdef __APPLE__
    QString set = settings.string(Settings::kSoundSet);
    if (!sets().contains(set)) {
        set = sets().front();
    }
    if (m_volume != volume || m_set != set) {
        // The set's three sounds as files at this volume, and macOS's handles for them.
        release();
        QDir().mkpath(m_folder);
        for (const Kind each : {Kind::Move, Kind::Capture, Kind::Check}) {
            const QByteArray data = wav(set, each, volume);
            if (data.isEmpty()) {
                continue;
            }
            // Named by what's in it: a sound changed in a new version is a new file.
            const QString path = QDir(m_folder).filePath(
                QStringLiteral("%1-%2-%3.wav")
                    .arg(set, fileName(each),
                         QString::fromLatin1(
                             QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex())));
            if (!QFileInfo::exists(path)) {
                QFile file(path);
                if (!file.open(QIODevice::WriteOnly) || file.write(data) < 0) {
                    continue;
                }
            }
            const QByteArray encoded = QFile::encodeName(path);
            CFURLRef url = CFURLCreateFromFileSystemRepresentation(
                kCFAllocatorDefault,
                // The path's bytes, as the C API takes them.
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                reinterpret_cast<const UInt8*>(encoded.constData()), encoded.size(),
                static_cast<Boolean>(false));
            if (url == nullptr) {
                continue;
            }
            SystemSoundID id = 0;
            if (AudioServicesCreateSystemSoundID(url, &id) == kAudioServicesNoError) {
                m_ids[static_cast<std::size_t>(each)] = id;
            }
            CFRelease(url);
        }
        m_volume = volume;
        m_set = std::move(set);
    }
    if (const std::uint32_t id = m_ids[static_cast<std::size_t>(kind)]; id != 0) {
        AudioServicesPlaySystemSound(id); // returns at once; macOS plays it
    }
#endif
}

} // namespace kiebitz::app
