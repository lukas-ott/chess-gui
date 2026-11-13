#pragma once

#include "document/GameDocument.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QFrame;
class QLabel;
class QProgressBar;
class QPushButton;

namespace kiebitz::repertoire {
class Drill;
} // namespace kiebitz::repertoire

namespace kiebitz::app {

class AppStyle;
class BoardView;
class PracticeStats;
class Sounds;

/// Practising a repertoire file, or a side's whole repertoire: the app plays the opponent's
/// moves from the file and you answer with yours on the board. It runs on the game it started
/// on and shows itself in a card that takes the moves' place (card()); the window shows that
/// card on started() and puts the moves back on stopped().
class Practice : public QObject {
    Q_OBJECT

public:
    /// `shown`: the game the board shows now, or null. Practice only acts while that is the game
    /// it started on.
    Practice(PracticeStats& stats, BoardView& board, Sounds& sounds, AppStyle& style,
             std::function<const GameDocument*()> shown, QObject* parent = nullptr);
    ~Practice() override;
    Practice(const Practice&) = delete;
    Practice& operator=(const Practice&) = delete;
    Practice(Practice&&) = delete;
    Practice& operator=(Practice&&) = delete;

    /// The card: what to play, how it went, Again and Stop.
    [[nodiscard]] QFrame* card() const { return m_card; }
    [[nodiscard]] bool active() const { return m_drill != nullptr; }
    /// The id of the game being practised (GameDocument::id); -1 before the first start.
    [[nodiscard]] int documentId() const noexcept { return m_document; }
    /// How the last practice was started (for Again).
    [[nodiscard]] bool fromHere() const noexcept { return m_fromHere; }
    [[nodiscard]] int depth() const noexcept { return m_depth; }
    [[nodiscard]] bool mistakesOnly() const noexcept { return m_mistakes; }

    /// Starts, or starts again, on `document` for `side`. `fileStart`: where the file starts
    /// (core::kNoNode: at the beginning). `mistakes`: only the lines through the positions you
    /// still get wrong. Nothing to practise: says so in message() and doesn't start.
    void start(const GameDocument& document, core::Color side, core::NodeId fileStart,
               bool fromHere, int depth, bool mistakes);
    void stop();
    /// A move made on the board while practising: your answer, or taken back if it isn't your
    /// turn.
    void answer(core::Move move);

signals:
    void started(core::Color side);
    void stopped();
    /// "Again" was clicked: the window starts it again as it was (it knows the game).
    void againRequested();
    /// For the status bar.
    void message(const QString& text);

private:
    void buildCard();
    [[nodiscard]] const GameDocument* document() const;
    /// The status line; `mood` > 0 right (a tick), < 0 wrong (a cross), 0 neither.
    void setStatus(const QString& text, int mood);
    void setWhy(const QString& text);
    /// After a move: the opponent's reply, the end of a line, or the end.
    void step();
    void showNode(core::NodeId id, bool animate, std::vector<core::Arrow> arrows = {});
    void updateProgress();

    PracticeStats& m_stats;
    BoardView& m_board;
    Sounds& m_sounds;
    AppStyle& m_style;
    std::function<const GameDocument*()> m_shown;

    std::unique_ptr<repertoire::Drill> m_drill;
    int m_document = -1; ///< the id of the document being practised
    int m_run = 0;       ///< bumped on every start and stop: late timers see it changed
    bool m_fromHere = false;
    int m_depth = 0;
    bool m_mistakes = false; ///< practising only your mistakes
    core::NodeId m_start = core::Game::root();

    QFrame* m_card = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_mark = nullptr; ///< a tick or a cross
    QLabel* m_status = nullptr;
    QLabel* m_why = nullptr; ///< the file's comment on the move you missed
    QProgressBar* m_bar = nullptr;
    QLabel* m_progress = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_again = nullptr;
};

} // namespace kiebitz::app
