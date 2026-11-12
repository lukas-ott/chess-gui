// GoogleTest main for the UI tests: they need a QApplication, which must exist before any widget.
// Runs offscreen (no window appears) unless QT_QPA_PLATFORM says otherwise.

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <gtest/gtest.h>

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    // Everything a run writes goes into a folder of its own, never the real app's: settings,
    // the library, caches, profiles, databases. One per process, so test processes can run
    // side by side (ctest runs them in parallel); it is removed at the end.
    const QString root = QDir::temp().filePath(
        QStringLiteral("kiebitz-ui-tests-%1").arg(QCoreApplication::applicationPid()));
    QDir(root).removeRecursively();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QDir(root).filePath(QStringLiteral("settings")));
    QApplication::setOrganizationName(QStringLiteral("KiebitzTests"));
    const QApplication app(argc, argv);
    QSettings().clear(); // each run starts fresh
    const QString library = QDir(root).filePath(QStringLiteral("library"));
    QSettings().setValue(QStringLiteral("library/folder"), library);
    const QString data = QDir(root).filePath(QStringLiteral("data"));
    qputenv("KIEBITZ_SETTINGS_DIR", data.toUtf8());
    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    QDir(root).removeRecursively();
    return result;
}
