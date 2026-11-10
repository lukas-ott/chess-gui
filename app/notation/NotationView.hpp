#pragma once

#include "kiebitz/core/game.hpp"

#include <QTextBrowser>
#include <QTextCharFormat>

#include <cstdint>
#include <unordered_map>

namespace kiebitz::app {

class AppStyle;

/// The game's moves as text: the main line, first-level variations on their own indented lines,
/// deeper ones inline in brackets, with move numbers, NAG symbols and comments.
///
/// Clicking a move selects it; right-clicking opens the editing menu (promote, make main line,
/// delete, annotate, comment). The view only shows the game and reports what the user asked
/// for through signals; the window changes the game and calls setGame()/setCurrent() back.
class NotationView : public QTextBrowser {
    Q_OBJECT

public:
    explicit NotationView(const AppStyle& style, QWidget* parent = nullptr);

    /// Shows a game (rebuilds the text). The game must outlive the view or the next setGame().
    void setGame(const core::Game* game, core::NodeId current);
    /// Moves the highlight. Cheap: doesn't rebuild the text.
    void setCurrent(core::NodeId current);

signals:
    void nodeActivated(kiebitz::core::NodeId id);
    void promoteRequested(kiebitz::core::NodeId id);
    void makeMainLineRequested(kiebitz::core::NodeId id);
    void deleteFromRequested(kiebitz::core::NodeId id);
    void deleteVariationRequested(kiebitz::core::NodeId id);
    void nagToggled(kiebitz::core::NodeId id, std::uint8_t nag);
    void nagsCleared(kiebitz::core::NodeId id);
    void commentEditRequested(kiebitz::core::NodeId id);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void render();
    void indexAnchors();
    [[nodiscard]] core::NodeId nodeAt(QPoint point) const;

    const AppStyle& m_style;
    const core::Game* m_game = nullptr;
    core::NodeId m_current = core::Game::root();

    struct Range {
        int start = 0;
        int length = 0;
    };
    std::unordered_map<core::NodeId, Range> m_ranges; ///< where each move's text is
    core::NodeId m_highlighted = core::kNoNode;
    QTextCharFormat m_savedFormat; ///< the highlighted move's format before highlighting
};

} // namespace kiebitz::app
