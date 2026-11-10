#pragma once

#include "chessgui/core/game.hpp"

#include <QWidget>

#include <vector>

class QComboBox;
class QLineEdit;

namespace chessgui::app {

/// The "Info" panel: edits a game's headers (players, ratings, event, date, result, ...).
/// Reports each change through headerEdited(); the window applies it to the game.
class GameInfoPanel : public QWidget {
    Q_OBJECT

public:
    explicit GameInfoPanel(QWidget* parent = nullptr);

    /// Shows a game's headers, or nothing (disabled) for nullptr.
    void setGame(const core::Game* game);

signals:
    void headerEdited(const QString& name, const QString& value);

private:
    struct Field {
        QString header;
        QLineEdit* edit;
    };
    std::vector<Field> m_fields;
    QComboBox* m_result = nullptr;
};

} // namespace chessgui::app
