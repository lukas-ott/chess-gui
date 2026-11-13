#include "GameDocument.hpp"

#include <QFileInfo>
#include <QObject>

namespace kiebitz::app {

QString GameDocument::title() const {
    const auto white = QString::fromStdString(std::string(game.header("White")));
    const auto black = QString::fromStdString(std::string(game.header("Black")));
    const auto known = [](const QString& name) {
        return !name.isEmpty() && name != u"?";
    };

    QString name;
    if (wholeRepertoire) {
        name = *wholeRepertoire == core::Color::White ? QObject::tr("White Repertoire")
                                                      : QObject::tr("Black Repertoire");
    } else if (known(white) || known(black)) {
        name = (known(white) ? white : QStringLiteral("?")) + QStringLiteral(" – ") +
               (known(black) ? black : QStringLiteral("?"));
    } else if (file && !file->path.isEmpty()) {
        name = QFileInfo(file->path).completeBaseName();
    } else {
        name = QObject::tr("New game");
    }
    return modified ? QStringLiteral("• ") + name : name;
}

std::vector<core::Move> lineTo(const core::Game& game, core::NodeId id) {
    std::vector<core::Move> line;
    for (const core::NodeId node : game.path(id)) {
        if (node != core::Game::root()) {
            line.push_back(game.node(node).move);
        }
    }
    return line;
}

} // namespace kiebitz::app
