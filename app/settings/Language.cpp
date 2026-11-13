#include "Language.hpp"

#include "settings/Settings.hpp"

#include <QCoreApplication>
#include <QLocale>
#include <QTranslator>

namespace kiebitz::app::language {

QString resolve(const QString& chosen) {
    if (chosen == u"en" || chosen == u"de" || chosen == u"nb") {
        return chosen;
    }
    for (const QString& wanted : QLocale::system().uiLanguages(QLocale::TagSeparator::Dash)) {
        const QString code = wanted.section(u'-', 0, 0).toLower();
        if (code == u"de") {
            return QStringLiteral("de");
        }
        if (code == u"nb" || code == u"no" || code == u"nn") {
            return QStringLiteral("nb");
        }
        if (code == u"en") {
            break;
        }
    }
    return QStringLiteral("en");
}

bool install(QTranslator& translator) {
    const QString chosen = Settings::instance().string(Settings::kLanguage);
    const QString lang = resolve(chosen);
    if (lang == u"en") {
        return false;
    }
    if (chosen != u"system") {
        QLocale::setDefault(QLocale(lang)); // dates and numbers as in that language
    }
    if (!translator.load(QStringLiteral(":/i18n/kiebitz_%1.qm").arg(lang))) {
        qWarning("Kiebitz: the %s translation couldn't be loaded", qPrintable(lang));
        return false;
    }
    return QCoreApplication::installTranslator(&translator);
}

} // namespace kiebitz::app::language
