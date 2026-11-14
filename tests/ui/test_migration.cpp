// Carrying settings and downloaded engines over from the app's old name (ChessGui).

#include "Migration.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <utility>

namespace {

using namespace kiebitz::app;

TEST(Migration, CopiesMissingSettingsAndNeverOverwrites) {
    const QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QSettings old(dir.filePath(QStringLiteral("old.ini")), QSettings::IniFormat);
    old.setValue(QStringLiteral("library/folder"), QStringLiteral("/Users/me/Documents/ChessGui"));
    old.setValue(QStringLiteral("board/theme"), QStringLiteral("Green"));
    old.setValue(QStringLiteral("window/geometry"), QStringLiteral("old"));
    // The new app already ran once (it saved its window) before anything was carried over.
    QSettings fresh(dir.filePath(QStringLiteral("new.ini")), QSettings::IniFormat);
    fresh.setValue(QStringLiteral("window/geometry"), QStringLiteral("new"));

    EXPECT_EQ(migration::copySettings(old, fresh), 2);
    EXPECT_EQ(fresh.value(QStringLiteral("library/folder")).toString(),
              QStringLiteral("/Users/me/Documents/ChessGui"));
    EXPECT_EQ(fresh.value(QStringLiteral("board/theme")).toString(), QStringLiteral("Green"));
    EXPECT_EQ(fresh.value(QStringLiteral("window/geometry")).toString(), QStringLiteral("new"));
    EXPECT_EQ(migration::copySettings(old, fresh), 0); // nothing left to copy
}

TEST(Migration, TheOldAppearanceBecomesATheme) {
    const QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    const QString theme = QStringLiteral("app/colourTheme");
    for (const auto& [appearance, expected] :
         {std::pair{"light", "daylight"}, std::pair{"dark", "graphite"}, std::pair{"system", ""}}) {
        settings.clear();
        settings.setValue(QStringLiteral("app/appearance"), QString::fromLatin1(appearance));
        EXPECT_TRUE(migration::appearanceToTheme(settings));
        EXPECT_EQ(settings.value(theme).toString(), QString::fromLatin1(expected)) << appearance;
        EXPECT_FALSE(settings.contains(QStringLiteral("app/appearance")));
        EXPECT_FALSE(migration::appearanceToTheme(settings)); // once
    }
    // A theme chosen already stays.
    settings.clear();
    settings.setValue(QStringLiteral("app/appearance"), QStringLiteral("light"));
    settings.setValue(theme, QStringLiteral("midnight"));
    EXPECT_TRUE(migration::appearanceToTheme(settings));
    EXPECT_EQ(settings.value(theme).toString(), QStringLiteral("midnight"));
}

TEST(Migration, MovesDownloadedEnginesAndTheirPaths) {
    const QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString oldEngines = dir.filePath(QStringLiteral("ChessGui/ChessGui/engines"));
    const QString newEngines = dir.filePath(QStringLiteral("Kiebitz/Kiebitz/engines"));
    ASSERT_TRUE(QDir().mkpath(oldEngines + QStringLiteral("/sf_19/stockfish")));
    QFile engine(oldEngines + QStringLiteral("/sf_19/stockfish/stockfish-macos-universal"));
    ASSERT_TRUE(engine.open(QIODevice::WriteOnly));
    engine.close();

    QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    settings.beginWriteArray(QStringLiteral("engines/list"), 2);
    settings.setArrayIndex(0);
    settings.setValue(QStringLiteral("name"), QStringLiteral("Stockfish 19"));
    settings.setValue(QStringLiteral("path"),
                      oldEngines + QStringLiteral("/sf_19/stockfish/stockfish-macos-universal"));
    settings.setArrayIndex(1); // one the user added from elsewhere: left alone
    settings.setValue(QStringLiteral("name"), QStringLiteral("Brew"));
    settings.setValue(QStringLiteral("path"), QStringLiteral("/opt/homebrew/bin/stockfish"));
    settings.endArray();

    EXPECT_TRUE(migration::moveEngines(oldEngines, newEngines));
    EXPECT_TRUE(
        QFile::exists(newEngines + QStringLiteral("/sf_19/stockfish/stockfish-macos-universal")));
    EXPECT_FALSE(QFile::exists(oldEngines));
    EXPECT_FALSE(migration::moveEngines(oldEngines, newEngines)); // done once

    // The paths are fixed even when the move happened on an earlier launch.
    EXPECT_EQ(migration::rewriteEnginePaths(oldEngines, newEngines, settings), 1);
    EXPECT_EQ(settings.value(QStringLiteral("engines/list/1/path")).toString(),
              newEngines + QStringLiteral("/sf_19/stockfish/stockfish-macos-universal"));
    EXPECT_EQ(settings.value(QStringLiteral("engines/list/2/path")).toString(),
              QStringLiteral("/opt/homebrew/bin/stockfish"));
    EXPECT_EQ(migration::rewriteEnginePaths(oldEngines, newEngines, settings), 0);
}

TEST(Migration, RenamesAFolderWhenOnlyItsCaseDiffers) {
    const QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(QDir(dir.path()).mkpath(QStringLiteral("engines/sf_19")));

    EXPECT_TRUE(migration::renameFolderCase(dir.path(), QStringLiteral("engines"),
                                            QStringLiteral("Engines")));
    const QStringList names = QDir(dir.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    EXPECT_EQ(names, QStringList{QStringLiteral("Engines")});
    EXPECT_TRUE(QFileInfo::exists(dir.filePath(QStringLiteral("Engines/sf_19"))));
    // Already renamed, or never there: nothing to do.
    EXPECT_FALSE(migration::renameFolderCase(dir.path(), QStringLiteral("engines"),
                                             QStringLiteral("Engines")));
}

} // namespace
