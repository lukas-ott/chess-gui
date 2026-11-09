#include "MainWindow.hpp"
#include "chessgui/core/version.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QTimer>

#include <cstdio>

namespace {

bool envFlag(const char* name) {
    return qEnvironmentVariableIntValue(name) != 0;
}

} // namespace

int main(int argc, char* argv[]) {
    // Measures time from main() to the first processed event after show().
    // CHESSGUI_STARTUP_TIMING=1 prints it; CHESSGUI_STARTUP_EXIT=1 also quits right away
    // (used by scripts/measure_startup.sh and CI).
    QElapsedTimer startupTimer;
    startupTimer.start();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ChessGui"));
    QApplication::setOrganizationName(QStringLiteral("ChessGui"));
    QApplication::setApplicationVersion(
        QString::fromUtf8(chessgui::core::version().data(),
                          static_cast<qsizetype>(chessgui::core::version().size())));

    chessgui::app::MainWindow window;
    window.show();

    if (envFlag("CHESSGUI_STARTUP_TIMING") || envFlag("CHESSGUI_STARTUP_EXIT")) {
        QTimer::singleShot(0, &app, [&startupTimer] {
            std::printf("startup_ms=%lld\n", static_cast<long long>(startupTimer.elapsed()));
            std::fflush(stdout);
            if (envFlag("CHESSGUI_STARTUP_EXIT")) {
                QApplication::quit();
            }
        });
    }

    return QApplication::exec();
}
