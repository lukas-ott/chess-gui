#include "GameDocument.hpp"

#include <QFileInfo>
#include <QObject>

namespace chessgui::app {

QString GameDocument::title() const {
    const auto white = QString::fromStdString(std::string(game.header("White")));
    const auto black = QString::fromStdString(std::string(game.header("Black")));
    const auto known = [](const QString& name) {
        return !name.isEmpty() && name != u"?";
    };

    QString name;
    if (known(white) || known(black)) {
        name = (known(white) ? white : QStringLiteral("?")) + QStringLiteral(" – ") +
               (known(black) ? black : QStringLiteral("?"));
    } else if (file && !file->path.isEmpty()) {
        name = QFileInfo(file->path).completeBaseName();
    } else {
        name = QObject::tr("New game");
    }
    return modified ? QStringLiteral("• ") + name : name;
}

} // namespace chessgui::app
