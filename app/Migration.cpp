#include "Migration.hpp"

#include "engine/EngineSettings.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace kiebitz::app::migration {
namespace {

constexpr auto kDoneKey = "migration/fromChessGui";

} // namespace

int copySettings(const QSettings& from, QSettings& to) {
    int copied = 0;
    for (const QString& key : from.allKeys()) {
        if (!to.contains(key)) {
            to.setValue(key, from.value(key));
            ++copied;
        }
    }
    to.sync();
    return copied;
}

bool moveEngines(const QString& oldFolder, const QString& newFolder) {
    if (!QFileInfo(oldFolder).isDir() || QFileInfo::exists(newFolder)) {
        return false;
    }
    QDir().mkpath(QFileInfo(newFolder).absolutePath());
    return QDir().rename(oldFolder, newFolder);
}

int rewriteEnginePaths(const QString& oldFolder, const QString& newFolder, QSettings& settings) {
    const QString oldPrefix = QDir(oldFolder).absolutePath() + QLatin1Char('/');
    const QString newPrefix = QDir(newFolder).absolutePath() + QLatin1Char('/');
    const int count = settings.beginReadArray(QStringLiteral("engines/list"));
    QStringList paths;
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        paths << settings.value(QStringLiteral("path")).toString();
    }
    settings.endArray();
    int changed = 0;
    for (int i = 0; i < count; ++i) {
        if (paths[i].startsWith(oldPrefix)) {
            settings.setValue(QStringLiteral("engines/list/%1/path").arg(i + 1),
                              newPrefix + paths[i].mid(oldPrefix.size()));
            ++changed;
        }
    }
    settings.sync();
    return changed;
}

void fromChessGui() {
    QSettings settings;
    // On macOS, settings also show system-wide keys (the "global domain"); only the app's own
    // count here.
    settings.setFallbacksEnabled(false);
    if (settings.value(QString::fromLatin1(kDoneKey)).toBool()) {
        return;
    }
    QSettings old(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("ChessGui"),
                  QStringLiteral("ChessGui"));
    old.setFallbacksEnabled(false);
    copySettings(old, settings);
    // Application Support/<organisation>/<app>: the old app used ChessGui for both.
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString oldEngines = QDir(data).filePath(QStringLiteral("ChessGui/ChessGui/engines"));
    if (moveEngines(oldEngines, engines::downloadFolder())) {
        // Leave no empty ChessGui folders behind (rmdir only removes empty ones).
        QDir(data).rmdir(QStringLiteral("ChessGui/ChessGui"));
        QDir(data).rmdir(QStringLiteral("ChessGui"));
    }
    // Also when the engines were moved on an earlier launch: the list must point at them.
    rewriteEnginePaths(oldEngines, engines::downloadFolder(), settings);
    settings.setValue(QString::fromLatin1(kDoneKey), true);
}

} // namespace kiebitz::app::migration
