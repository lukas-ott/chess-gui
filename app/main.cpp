#include "MainWindow.hpp"
#include "Migration.hpp"
#include "kiebitz/core/version.hpp"
#include "style/AppStyle.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QIcon>
#include <QSettings>
#include <QTimer>

#include <cstdio>

namespace {

bool envFlag(const char* name) {
    return qEnvironmentVariableIntValue(name) != 0;
}

} // namespace

int main(int argc, char* argv[]) {
    // Measures time from main() to the first processed event after show().
    // KIEBITZ_STARTUP_TIMING=1 prints it; KIEBITZ_STARTUP_EXIT=1 also quits right away
    // (used by scripts/measure_startup.sh and CI).
    QElapsedTimer startupTimer;
    startupTimer.start();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Kiebitz"));
    // The Dock icon. The bundle has it too (Info.plist), but macOS caches what an app looked
    // like when it first saw it; setting it here always shows the current one.
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon/Kiebitz.png")));
    QApplication::setOrganizationName(QStringLiteral("Kiebitz"));
    // Tests that run the real app use throwaway settings, so they never change the user's.
    if (const QString dir = qEnvironmentVariable("KIEBITZ_SETTINGS_DIR"); !dir.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
    } else {
        // The app was called ChessGui: bring its settings and engines along, once.
        kiebitz::app::migration::fromChessGui();
    }
    QApplication::setApplicationVersion(QString::fromUtf8(
        kiebitz::core::version().data(), static_cast<qsizetype>(kiebitz::core::version().size())));

    // Style first, with the saved appearance, so the window never flashes in the wrong colours.
    kiebitz::app::AppStyle style(app);
    style.setAppearance(kiebitz::app::AppStyle::fromString(
        QSettings().value(QStringLiteral("app/appearance")).toString()));

    kiebitz::app::MainWindow window(style);
    window.show();

    if (envFlag("KIEBITZ_STARTUP_TIMING") || envFlag("KIEBITZ_STARTUP_EXIT")) {
        QTimer::singleShot(0, &app, [&startupTimer] {
            std::printf("startup_ms=%lld\n", static_cast<long long>(startupTimer.elapsed()));
            std::fflush(stdout);
            if (envFlag("KIEBITZ_STARTUP_EXIT")) {
                QApplication::quit();
            }
        });
    }

    return QApplication::exec();
}
