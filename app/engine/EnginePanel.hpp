#pragma once

#include "AnalysisSource.hpp"

#include <QStringList>
#include <QWidget>

#include <optional>
#include <vector>

class QToolButton;
class QVBoxLayout;

namespace kiebitz::app {

class AppStyle;
class EngineRegistry;
class EngineSection;

/// The Engine tab beside the board: every engine the user has (and the Lichess cloud), each in
/// its own section with its own Start/Stop, so several can analyse the board at once. Engines are
/// added and set up on the Engines page; this tab runs them.
///
/// Nothing runs until the user starts an engine. The eval bar and the board's arrows follow the
/// topmost running engine.
class EnginePanel : public QWidget {
    Q_OBJECT

public:
    EnginePanel(EngineRegistry& registry, const AppStyle& style, QWidget* parent = nullptr);

    /// The position to analyse; nullopt when no game is shown (analysis pauses).
    void setPosition(std::optional<core::Position> position);

    /// Starts or stops one engine (by name, or cloudName()).
    void setRunning(const QString& name, bool running);
    [[nodiscard]] bool isRunning(const QString& name) const;
    /// Whether any engine runs.
    [[nodiscard]] bool isRunning() const;
    /// Space: stops every running engine, or starts again the ones that ran last (the first
    /// engine if none did yet).
    void toggle();

    [[nodiscard]] EngineSection* section(const QString& name) const;
    /// The topmost running engine: the one the eval bar and the arrows show.
    [[nodiscard]] EngineSection* primary() const;
    /// The primary engine's lines (empty when nothing runs).
    [[nodiscard]] const std::vector<engine::Info>& lines() const;

    [[nodiscard]] static QString cloudName();

signals:
    /// What the board and eval bar should show (from the primary engine): the first move of
    /// each line, best first, and the best line's score from White's view. `running` is false
    /// when no engine runs.
    void analysisChanged(const std::vector<kiebitz::core::Move>& bestMoves,
                         std::optional<kiebitz::engine::Score> score, bool running);
    /// The user clicked a move in a line: play these moves from the current position.
    void playRequested(const std::vector<kiebitz::core::Move>& moves);
    /// Go to the Engines page (to add or set up engines).
    void manageEnginesRequested();

private:
    void rebuildSections();
    void publish();
    void rememberRunning();
    void updateIcons();

    EngineRegistry& m_registry;
    const AppStyle& m_style;
    std::vector<EngineSection*> m_sections; ///< the user's engines in order, then the cloud
    std::optional<core::Position> m_position;
    QStringList m_lastRunning; ///< what Space starts again

    QToolButton* m_manage = nullptr;
    QWidget* m_noEngine = nullptr; ///< "no engine on this Mac yet", with a way to get one
    QVBoxLayout* m_sectionLayout = nullptr;
};

} // namespace kiebitz::app
