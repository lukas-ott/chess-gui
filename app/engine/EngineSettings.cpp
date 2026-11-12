#include "EngineSettings.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <utility>

namespace kiebitz::app {
namespace {

constexpr auto kListKey = "engines/list";
constexpr auto kOldLinesKey = "engine/lines"; ///< the Engine tab's line count (before M5)
constexpr auto kCloudLinesKey = "engines/cloudLines";

} // namespace

int EngineEntry::defaultThreads() {
    return std::max(1, QThread::idealThreadCount() / 2);
}

int EngineEntry::defaultLines() {
    return std::clamp(QSettings().value(QString::fromLatin1(kOldLinesKey), 3).toInt(), 1,
                      engines::kMaxLines);
}

namespace engines {

int cloudLines() {
    return std::clamp(
        QSettings().value(QString::fromLatin1(kCloudLinesKey), EngineEntry::defaultLines()).toInt(),
        1, kMaxLines);
}

void setCloudLines(int lines) {
    QSettings().setValue(QString::fromLatin1(kCloudLinesKey), std::clamp(lines, 1, kMaxLines));
}

std::vector<EngineEntry> load() {
    QSettings settings;
    std::vector<EngineEntry> list;
    const int count = settings.beginReadArray(QString::fromLatin1(kListKey));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        EngineEntry entry;
        entry.name = settings.value(QStringLiteral("name")).toString();
        entry.path = settings.value(QStringLiteral("path")).toString();
        entry.threads = settings.value(QStringLiteral("threads"), entry.threads).toInt();
        entry.hashMb = settings.value(QStringLiteral("hash"), entry.hashMb).toInt();
        entry.lines =
            std::clamp(settings.value(QStringLiteral("lines"), entry.lines).toInt(), 1, kMaxLines);
        if (!entry.name.isEmpty() && !entry.path.isEmpty()) {
            list.push_back(entry);
        }
    }
    settings.endArray();
    return list;
}

void save(const std::vector<EngineEntry>& list) {
    QSettings settings;
    settings.remove(QString::fromLatin1(kListKey));
    settings.beginWriteArray(QString::fromLatin1(kListKey), static_cast<int>(list.size()));
    for (int i = 0; std::cmp_less(i, list.size()); ++i) {
        const EngineEntry& entry = list[static_cast<std::size_t>(i)];
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), entry.name);
        settings.setValue(QStringLiteral("path"), entry.path);
        settings.setValue(QStringLiteral("threads"), entry.threads);
        settings.setValue(QStringLiteral("hash"), entry.hashMb);
        settings.setValue(QStringLiteral("lines"), entry.lines);
    }
    settings.endArray();
}

QString findInstalledStockfish() {
    for (const char* path : {"/opt/homebrew/bin/stockfish", "/usr/local/bin/stockfish"}) {
        const QFileInfo info(QString::fromLatin1(path));
        if (info.isFile() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

QString downloadFolder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("Engines"));
}

} // namespace engines
} // namespace kiebitz::app
