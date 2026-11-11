#pragma once

#include "kiebitz/core/game.hpp"

#include <QWidget>

#include <array>
#include <cstdint>
#include <unordered_set>

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
///  - for any other game: the repertoire check, for the side the board is viewed from. Whether
///    the game is still in your repertoire at this move ("In book · Philidor Defense"), or where
///    it left it ("Left the book with 7. f3 · Philidor Defense plays 7. Be3"). It names one
///    file: the one that follows the game the longest, so it stays put from move to move. Buttons
///    jump to that file's line and add the game's line.
class RepertoirePanel : public QWidget {
    Q_OBJECT

public:
    RepertoirePanel(RepertoireStore& store, const AppStyle& style, QWidget* parent = nullptr);

    /// The game shown and its current move. `repertoireFile`: it is a repertoire for `side`.
    /// The game must outlive the panel or the next call (null: no game).
    /// `mistakes`: as setMistakes().
    void setGame(const core::Game* game, core::NodeId current, bool repertoireFile,
                 core::Color side, const std::unordered_set<std::uint64_t>* mistakes = nullptr);

    /// The side the board is viewed from: the check is for that side's repertoire.
    void setViewSide(core::Color side);

    /// The positions you still get wrong for the side of the file shown (null: not known yet).
    /// They must outlive the panel or the next call.
    void setMistakes(const std::unordered_set<std::uint64_t>* mistakes);

    /// The practice depth (up to this move).
    [[nodiscard]] int practiceDepth() const;

    /// For tests: what the check says for a side, and the practice counts.
    [[nodiscard]] QString statusText(core::Color side) const;
    [[nodiscard]] QString practiceText() const;

signals:
    void addLineRequested(kiebitz::core::Color side);
    /// Open a repertoire file at a position (its hash).
    void jumpRequested(const QString& path, quint64 position);
    /// Practise the whole file (from the start) or from the current position, up to `depth`.
    void practiceRequested(bool fromHere, int depth);
    /// Practise only the lines through the positions you got wrong.
    void mistakesRequested();

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
    core::Color m_viewSide = core::Color::White;

    QStackedWidget* m_pages = nullptr; ///< 0 = nothing to check, 1 = the check, 2 = practice
    struct Row {
        QWidget* widget = nullptr;
        QLabel* title = nullptr;
        QLabel* status = nullptr;
        QPushButton* add = nullptr;
        QPushButton* jump = nullptr;
        QString file;       ///< the file named (the longest match); kept while it fits
        quint64 jumpTo = 0; ///< where Jump to Line goes in it
    };
    std::array<Row, 2> m_rows; ///< White, Black

    QLabel* m_practiceCount = nullptr;
    Stepper* m_depth = nullptr;
    QPushButton* m_practiceAll = nullptr;
    QPushButton* m_practiceHere = nullptr;
    QPushButton* m_practiceMistakes = nullptr;
    const std::unordered_set<std::uint64_t>* m_mistakes = nullptr;
};

} // namespace kiebitz::app
