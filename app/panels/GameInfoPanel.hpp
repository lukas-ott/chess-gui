#pragma once

#include "kiebitz/core/game.hpp"

#include <QWidget>

#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;

namespace kiebitz::app {

/// The "Info" panel: edits a game's headers (players, ratings, event, date, result, ...).
/// For a repertoire file it shows what a repertoire has instead: its name, the side it is for,
/// the opening, its author and date.
/// Reports each change through headerEdited(); the window applies it to the game.
class GameInfoPanel : public QWidget {
    Q_OBJECT

public:
    explicit GameInfoPanel(QWidget* parent = nullptr);

    /// Shows a game's headers, or nothing (disabled) for nullptr. `repertoire`: the game is
    /// (a chapter of) a repertoire file.
    void setGame(const core::Game* game, bool repertoire = false);

signals:
    void headerEdited(const QString& name, const QString& value);

private:
    struct Field {
        QString header;
        QLineEdit* edit;
    };
    std::vector<Field> m_fields;
    QComboBox* m_result = nullptr;
    QComboBox* m_side = nullptr; ///< a repertoire's [Orientation]
    struct Row {
        std::vector<QWidget*> widgets;
        bool games = true;       ///< shown for games
        bool repertoires = true; ///< shown for repertoire files
    };
    std::vector<Row> m_rows;
    QLabel* m_eventLabel = nullptr; ///< "Event", or "Name" for a repertoire
};

} // namespace kiebitz::app
