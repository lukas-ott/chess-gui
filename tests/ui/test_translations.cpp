// The app's translations: German and Norwegian (Bokmål), compiled into the app. Every text is
// translated, and the pages still fit with the longer words.

#include "MainWindow.hpp"
#include "settings/Language.hpp"
#include "settings/Settings.hpp"
#include "style/AppStyle.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QMenuBar>
#include <QTest>
#include <QTranslator>
#include <gtest/gtest.h>

namespace {

using kiebitz::app::MainWindow;

QString translate(const QTranslator& translator, const char* context, const char* text,
                  int n = -1) {
    return translator.translate(context, text, nullptr, n);
}

TEST(Translations, GermanAndNorwegianAreInTheApp) {
    for (const auto& [language, file, open] :
         {std::tuple{"de", "&Datei", "Diese Datei hat 3 Partien. Welche?"},
          std::tuple{"nb", "&Fil", "Denne filen har 3 partier. Hvilket?"}}) {
        QTranslator translator;
        ASSERT_TRUE(translator.load(
            QStringLiteral(":/i18n/kiebitz_%1.qm").arg(QString::fromLatin1(language))))
            << language;
        EXPECT_FALSE(translator.isEmpty());
        EXPECT_EQ(translate(translator, "kiebitz::app::MainWindow", "&File"),
                  QString::fromUtf8(file));
        EXPECT_EQ(translate(translator, "kiebitz::app::Documents",
                            "This file has %n game(s). Which one?", 3)
                      .replace(QStringLiteral("%n"), QStringLiteral("3")),
                  QString::fromUtf8(open));
    }
}

TEST(Translations, TheLanguageChosenIsTheOneTheAppStartsIn) {
    EXPECT_EQ(kiebitz::app::language::resolve(QStringLiteral("de")), QStringLiteral("de"));
    EXPECT_EQ(kiebitz::app::language::resolve(QStringLiteral("nb")), QStringLiteral("nb"));
    EXPECT_EQ(kiebitz::app::language::resolve(QStringLiteral("en")), QStringLiteral("en"));
    // As main() does at the start: Deutsch chosen in Settings, then the window.
    auto& settings = kiebitz::app::Settings::instance();
    settings.setValue(kiebitz::app::Settings::kLanguage, QStringLiteral("de"));
    QTranslator translator;
    ASSERT_TRUE(kiebitz::app::language::install(translator));
    {
        kiebitz::app::AppStyle style(*qApp);
        const MainWindow window(style);
        const QStringList menus = [&] {
            QStringList titles;
            for (const QAction* menu : window.menuBar()->actions()) {
                titles << menu->text();
            }
            return titles;
        }();
        EXPECT_EQ(menus.value(0), QStringLiteral("&Datei")) << menus.join(u',').toStdString();
        EXPECT_EQ(QLocale().language(), QLocale::German); // dates and numbers too
    }
    QApplication::removeTranslator(&translator);
    QLocale::setDefault(QLocale::system());
    settings.setValue(kiebitz::app::Settings::kLanguage, QStringLiteral("system"));
}

TEST(Translations, NoTextIsLeftUntranslated) {
    for (const char* language : {"de", "nb"}) {
        QFile file(QStringLiteral(KIEBITZ_SOURCE_DIR "/app/translations/kiebitz_%1.ts")
                       .arg(QLatin1String(language)));
        ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << language;
        const QByteArray text = file.readAll();
        EXPECT_EQ(text.count("type=\"unfinished\""), 0) << language;
        EXPECT_EQ(text.count("<translation></translation>"), 0) << language;
    }
}

TEST(Translations, Snapshots) {
    const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR");
    if (dir.isEmpty()) {
        GTEST_SKIP() << "set KIEBITZ_SNAPSHOT_DIR";
    }
    for (const char* language : {"de", "nb"}) {
        QTranslator translator;
        ASSERT_TRUE(
            translator.load(QStringLiteral(":/i18n/kiebitz_%1.qm").arg(QLatin1String(language))));
        QApplication::installTranslator(&translator);
        {
            kiebitz::app::AppStyle style(*qApp);
            MainWindow window(style);
            window.show();
            window.setWindowState(Qt::WindowNoState);
            window.resize(1280, 800);
            for (const auto& [section, name] :
                 {std::pair{MainWindow::Section::Board, "board"},
                  std::pair{MainWindow::Section::Library, "library"},
                  std::pair{MainWindow::Section::Training, "training"},
                  std::pair{MainWindow::Section::Puzzles, "puzzles"},
                  std::pair{MainWindow::Section::Profiles, "profiles"},
                  std::pair{MainWindow::Section::Settings, "settings"}}) {
                window.showSection(section);
                QTest::qWait(150);
                window.grab().save(QDir(dir).filePath(
                    QStringLiteral("%1-%2.png").arg(QLatin1String(language), QLatin1String(name))));
            }
        }
        QApplication::removeTranslator(&translator);
    }
}

} // namespace
