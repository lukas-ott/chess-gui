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
                       QDir::temp().filePath(QStringLiteral("chessgui-ui-tests")));
    QApplication::setOrganizationName(QStringLiteral("ChessGuiTests"));
    const QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
