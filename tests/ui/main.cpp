// GoogleTest main for the UI tests: they need a QApplication, which must exist before any widget.
// Runs offscreen (no window appears) unless QT_QPA_PLATFORM says otherwise.

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <gtest/gtest.h>

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    // Settings go to a throwaway INI file, never to the real app's preferences.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QDir::temp().filePath(QStringLiteral("kiebitz-ui-tests")));
    QApplication::setOrganizationName(QStringLiteral("KiebitzTests"));
    const QApplication app(argc, argv);
    // A throwaway library folder too: tests must never touch the real library
    // (~/Documents/Kiebitz).
    const QString library = QDir::temp().filePath(QStringLiteral("kiebitz-ui-tests-library"));
    QDir(library).removeRecursively();
    QSettings().setValue(QStringLiteral("library/folder"), library);
    // And a throwaway library cache (Library::defaultCacheFile looks here).
    const QString cache = QDir::temp().filePath(QStringLiteral("kiebitz-ui-tests-cache"));
    QDir(cache).removeRecursively();
    qputenv("KIEBITZ_SETTINGS_DIR", cache.toUtf8());
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
