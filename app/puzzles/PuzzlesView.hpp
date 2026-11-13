#pragma once

#include "board/BoardView.hpp"
#include "kiebitz/core/game.hpp"
#include "kiebitz/puzzles/puzzle.hpp"
#include "puzzles/PuzzleStats.hpp"

#include <QElapsedTimer>
#include <QWidget>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <vector>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace kiebitz::puzzles {
class PuzzleDb;
}

namespace kiebitz::app {

class AppStyle;
class EmptyState;
class Library;
class LichessPuzzles;
class PageHeader;
class Sounds;
class Stepper;
class Toggle;
struct BoardTheme;

/// The Puzzles page: tactics to solve on a board, from the Lichess puzzle database (picked by
/// rating and theme) or from a puzzle file of the Library (in its order). A wrong move counts as
/// a miss and is taken back, so you can try again; a hint or the solution helps, and counts too.
/// No rating of your own: how many you solve cleanly, in a row, and how fast.
class PuzzlesView : public QWidget {
    Q_OBJECT

public:
    PuzzlesView(LichessPuzzles& lichess, PuzzleStats& stats, Library& library, Sounds& sounds,
                const AppStyle& style, QWidget* parent = nullptr);
    ~PuzzlesView() override;
    PuzzlesView(const PuzzlesView&) = delete;
    PuzzlesView& operator=(const PuzzlesView&) = delete;
    PuzzlesView(PuzzlesView&&) = delete;
    PuzzlesView& operator=(PuzzlesView&&) = delete;

    static constexpr auto kLichess = "lichess"; ///< the source key of the Lichess puzzles

    void setBoardLook(const BoardTheme& theme, const QString& pieceSet);
    void setBoardOptions(const BoardView::Options& options);
    /// Your highest rating (0: none known). Until you choose a rating range yourself, the
    /// Lichess puzzles are picked around it; called again when it may have changed.
    void setRatingSource(std::function<int()> highest);
    void followRating();

    /// Shows the puzzles of a source: kLichess, or a puzzle file's path.
    void setSource(const QString& source);
    [[nodiscard]] QString source() const { return m_source; }
    /// The next puzzle of the source (a new one from Lichess; the next one not yet solved
    /// cleanly from a file).
    void nextPuzzle();
    /// For a puzzle file: that puzzle (its index in the file).
    void showFilePuzzle(int index);

    // For tests
    [[nodiscard]] BoardView* board() const { return m_board; }
    [[nodiscard]] const puzzles::Solve* solving() const { return m_solve ? &*m_solve : nullptr; }
    [[nodiscard]] QString prompt() const;
    [[nodiscard]] QStringList listRows() const;
    /// Called when the page is shown: opens the Lichess puzzles if they're there.
    void activate();

signals:
    /// "Analyse": the puzzle, as a game to open on the board.
    void analyseRequested(const kiebitz::core::Game& game);
    /// A puzzle is on the board, ready for your move (tests).
    void puzzleShown();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct FilePuzzle {
        QString name;                          ///< "Aufgabe 1 · Standardmotiv am Damenflügel"
        std::string key;                       ///< for the history: its position and name
        std::optional<puzzles::Puzzle> puzzle; ///< nothing: no solution in the file
    };

    void refreshSources();
    void openLichess();
    void showLichessState();
    void loadFile(const QString& path);
    void show(const puzzles::Puzzle& puzzle, const std::string& key);
    void startSolving();
    void onMove(core::Move move);
    void playReply();
    void hint();
    void showSolution();
    void solutionStep(int run);
    void finish(bool solved);
    void setPrompt(const QString& text, const char* mood);
    void showDetails();
    void showNode(core::NodeId id, bool animate);
    void refreshStats();
    void fillList();
    void updateClock();
    void toggleClock();
    [[nodiscard]] qint64 clockMillis() const;
    void fitClock();
    void saveFilters() const;
    void stepReview(int plies);
    [[nodiscard]] bool isLichess() const;

    LichessPuzzles& m_lichess;
    PuzzleStats& m_stats;
    Library& m_library;
    Sounds& m_sounds;
    const AppStyle& m_style;

    PageHeader* m_header = nullptr;
    QComboBox* m_sources = nullptr;
    BoardView* m_board = nullptr;
    QStackedWidget* m_side = nullptr; ///< the Lichess download, or the puzzle
    // The download
    EmptyState* m_downloadText = nullptr;
    QPushButton* m_download = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_progressText = nullptr;
    QPushButton* m_cancel = nullptr;
    // The puzzle
    QLabel* m_prompt = nullptr;  ///< "Black to move" / "Right! Keep going." / "Solved"
    QLabel* m_details = nullptr; ///< rating and themes, or the puzzle's name
    QLabel* m_clock = nullptr;
    QPushButton* m_pause = nullptr; ///< stops and starts the clock
    QPushButton* m_hint = nullptr;
    QPushButton* m_solution = nullptr;
    QPushButton* m_analyse = nullptr;
    QPushButton* m_next = nullptr;
    std::array<QLabel*, 4> m_tiles{}; ///< solved, accuracy, streak, average time
    QWidget* m_filters = nullptr;     ///< Lichess only
    Stepper* m_low = nullptr;
    Stepper* m_high = nullptr;
    QComboBox* m_theme = nullptr;
    Toggle* m_progressive = nullptr;
    Toggle* m_autoNext = nullptr;
    Toggle* m_hideRating = nullptr;
    QLabel* m_listTitle = nullptr;
    QTreeWidget* m_list = nullptr; ///< your latest puzzles (Lichess) or the file's puzzles

    QString m_source;
    std::function<int()> m_highestRating;
    bool m_started = false; ///< the page was shown, and its source opened
    std::shared_ptr<const puzzles::PuzzleDb> m_db;
    std::vector<FilePuzzle> m_filePuzzles;
    int m_fileIndex = -1;
    int m_fileRequest = 0;
    int m_pickRequest = 0;
    std::mt19937_64 m_random;
    PuzzleStats::Summary m_summary;

    std::optional<puzzles::Solve> m_solve;
    std::optional<puzzles::Puzzle> m_puzzle;
    std::string m_key;      ///< the puzzle's key in the history
    bool m_helped = false;  ///< a hint or the solution
    bool m_touched = false; ///< any move or help: skipping it counts as a miss
    bool m_finished = false;
    std::optional<core::Move> m_hintShown; ///< the first hint (the piece) was given
    QElapsedTimer m_elapsed;
    qint64 m_millis = 0;      ///< when finished
    qint64 m_clockMillis = 0; ///< counted before the clock was last stopped
    bool m_clockStopped = false;
    QTimer* m_tick = nullptr;
    int m_run = 0;                      ///< newer puzzles cancel older delayed steps
    std::vector<core::NodeId> m_review; ///< the solution's line, after it's done
    std::size_t m_reviewAt = 0;
};

} // namespace kiebitz::app
