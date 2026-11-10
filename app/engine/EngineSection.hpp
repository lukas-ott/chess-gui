#pragma once

#include "AnalysisSource.hpp"
#include "EngineSettings.hpp"

#include <QWidget>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QLabel;
class QPushButton;

namespace kiebitz::app {

class AppStyle;
class UciEngine;

/// One engine in the Engine tab: its name, a Start/Stop button, what it is based on (depth,
/// speed) and its lines. Each section runs its own engine, so several can analyse at once.
class EngineSection : public QWidget {
    Q_OBJECT

public:
    /// `entry` is a UCI engine; nullopt means the Lichess cloud (named `name`).
    EngineSection(const QString& name, std::optional<EngineEntry> entry, const AppStyle& style,
                  QWidget* parent = nullptr);
    ~EngineSection() override;
    EngineSection(const EngineSection&) = delete;
    EngineSection& operator=(const EngineSection&) = delete;
    EngineSection(EngineSection&&) = delete;
    EngineSection& operator=(EngineSection&&) = delete;

    [[nodiscard]] const QString& name() const noexcept { return m_name; }
    void setPosition(std::optional<core::Position> position);
    void setRunning(bool running);
    [[nodiscard]] bool isRunning() const noexcept { return m_running; }
    void setLineCount(int lines);
    /// New settings for a UCI engine: a running one restarts with them.
    void setEntry(const EngineEntry& entry);

    /// The lines on show, best first; empty when off.
    [[nodiscard]] const std::vector<engine::Info>& lines() const noexcept { return m_lines; }
    /// The first move of each line, best first, as legal moves in the current position.
    [[nodiscard]] std::vector<core::Move> bestMoves() const;
    /// The best line's score from White's view (or the game's result when it is over).
    [[nodiscard]] std::optional<engine::Score> score() const;
    /// The engine process, if this is a UCI engine that has started (for tests).
    [[nodiscard]] UciEngine* uciEngine() const;
    /// Plays the first `plies` moves of line `line` (what clicking a move in it does).
    void playLine(int line, int plies);

    static constexpr int kMaxLines = 5;

signals:
    /// Started, stopped, or new lines: the board may need to show something else.
    void changed();
    void playRequested(const std::vector<kiebitz::core::Move>& moves);

private:
    void ensureSource();
    void dropSource();
    void analyseCurrent();
    void onUpdated(const AnalysisUpdate& update);
    void showLines();
    void showStatus(const QString& text);

    QString m_name;
    std::optional<EngineEntry> m_entry;
    const AppStyle& m_style;
    std::unique_ptr<AnalysisSource> m_source;
    std::optional<core::Position> m_position;
    bool m_running = false;
    int m_multiPv = 3;
    std::vector<engine::Info> m_lines;
    std::optional<engine::Score> m_over; ///< the game is over on the board: its result
    QString m_status;

    QPushButton* m_toggle = nullptr;
    QLabel* m_statusLabel = nullptr;
    struct LineRow {
        QWidget* row = nullptr;
        QLabel* score = nullptr;
        QLabel* moves = nullptr;
    };
    std::array<LineRow, kMaxLines> m_rows{};
};

} // namespace kiebitz::app
