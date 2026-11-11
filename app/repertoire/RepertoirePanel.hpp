#pragma once

#include "kiebitz/core/game.hpp"

#include <QWidget>

#include <array>

class QLabel;
class QPushButton;
class QStackedWidget;

namespace kiebitz::app {

class AppStyle;
class RepertoireStore;
class Stepper;

/// The Repertoire tab beside the board. It has two faces:
///  - for a repertoire file: practice. How many positions there are to practise up to a chosen
///    move, and buttons to practise the whole file or the branch from the current position;
///  - for any other game: the repertoire check. For each side, whether the game is still in your
///    repertoire files at this move and in which file ("In book · Philidor Defense"), or where it
///    left them ("Left the book with 7. f3 · Philidor Defense plays 7. Be3"), with a button to
///    add the game's line.
class RepertoirePanel : public QWidget {
    Q_OBJECT

public:
    RepertoirePanel(RepertoireStore& store, const AppStyle& style, QWidget* parent = nullptr);

    /// The game shown and its current move. `repertoireFile`: it is a repertoire for `side`.
    /// The game must outlive the panel or the next call (null: no game).
    void setGame(const core::Game* game, core::NodeId current, bool repertoireFile,
                 core::Color side);

    /// The practice depth (up to this move).
    [[nodiscard]] int practiceDepth() const;

    /// For tests: what the check says for a side, and the practice counts.
    [[nodiscard]] QString statusText(core::Color side) const;
    [[nodiscard]] QString practiceText() const;

signals:
    void addLineRequested(kiebitz::core::Color side);
    /// Practise the whole file (from the start) or from the current position, up to `depth`.
    void practiceRequested(bool fromHere, int depth);

private:
    void refresh();
    void refreshCheck();
    void refreshPractice();

    RepertoireStore& m_store;
    const AppStyle& m_style;
    const core::Game* m_game = nullptr;
    core::NodeId m_current = core::Game::root();
    bool m_repertoireFile = false;
    core::Color m_side = core::Color::White;

    QStackedWidget* m_pages = nullptr; ///< 0 = nothing to check, 1 = the check, 2 = practice
    struct Row {
        QWidget* widget = nullptr;
        QLabel* title = nullptr;
        QLabel* status = nullptr;
        QPushButton* add = nullptr;
    };
    std::array<Row, 2> m_rows; ///< White, Black

    QLabel* m_practiceCount = nullptr;
    Stepper* m_depth = nullptr;
    QPushButton* m_practiceAll = nullptr;
    QPushButton* m_practiceHere = nullptr;
};

} // namespace kiebitz::app
