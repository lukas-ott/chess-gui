#pragma once

#include "kiebitz/core/types.hpp"
#include "kiebitz/engine/score.hpp"

#include <QWidget>

#include <array>
#include <optional>
#include <vector>

class QLabel;
class QSettings;
class QSplitter;
class QStackedWidget;

namespace kiebitz::app {

class BoardView;
class EvalBar;

/// A game tab's page: the board on the left, with the eval bar beside it and the players'
/// clocks above and below; on the right the moves (or the practice card in their place) above
/// the panels. The board's column is kept exactly as wide as the board is tall, and the eval
/// bar's and the clocks' room is always kept, so the board never moves when they come and go.
class BoardPage : public QWidget {
    Q_OBJECT

public:
    explicit BoardPage(QWidget* parent = nullptr);

    /// The right-hand column: the moves card, the practice card (shown instead of the moves
    /// while practising) and the panels below them. Called once.
    void setSide(QWidget* moves, QWidget* practice, QWidget* panels);

    [[nodiscard]] BoardView* board() const { return m_board; }
    [[nodiscard]] bool isFlipped() const;
    /// The board and the eval bar from Black's side, or White's.
    void setFlipped(bool flipped);
    void showPractice(bool on);
    /// The engine's best line on the board and its score in the bar; the bar shows while it runs.
    void setAnalysis(const std::vector<core::Move>& best, std::optional<engine::Score> score,
                     bool running);
    /// The players' last clocks, White's and Black's; empty: none shown for that side.
    void setClocks(const std::array<QString, 2>& whiteBlack);

    /// The board's column as wide as the board is tall; the rest to the side column.
    void fit();
    /// The dividers where the user left them (a first launch: the moves get two thirds).
    void restoreLayout(const QSettings& settings);
    void saveLayout(QSettings& settings) const;

    /// The board's size changes (the eval bar follows) and its wheel steps.
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    /// The wheel turned over the board: -1 a move back, +1 a move forward (one per notch).
    void wheelStep(int step);

private:
    /// Puts the eval bar beside the board: as tall as its squares, centred in the gap; the
    /// clocks just above and below the squares.
    void placeEvalBar();

    BoardView* m_board = nullptr;
    EvalBar* m_evalBar = nullptr;
    std::array<QLabel*, 2> m_clocks{}; ///< above and below the board (float like the eval bar)
    QSplitter* m_mainSplitter = nullptr;
    QSplitter* m_sideSplitter = nullptr;
    QStackedWidget* m_movesStack = nullptr; ///< 0 = the moves, 1 = practice
    bool m_defaultSideSplit = false;        ///< no saved layout: keep the moves at 55 %
    int m_wheel = 0; ///< wheel turned over the board, not yet a whole step (1/8°)
};

} // namespace kiebitz::app
