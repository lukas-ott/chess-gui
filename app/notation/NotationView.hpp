#pragma once

#include "kiebitz/core/game.hpp"

#include <QAbstractScrollArea>
#include <QFont>
#include <QHash>
#include <QString>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kiebitz::app {

class AppStyle;

/// The game's moves as text: the main line, first-level variations on their own indented lines,
/// deeper ones inline in brackets, with move numbers, NAG symbols and comments.
///
/// Clicking a move selects it; right-clicking opens the editing menu (promote, make main line,
/// delete, annotate, comment). The view only shows the game and reports what the user asked
/// for through signals; the window changes the game and calls setGame()/setCurrent() back.
///
/// Built for big repertoires (50,000 positions): the text is a flat list of words, each word's
/// width is measured once per font (moves like "Nf3" repeat endlessly), lines are laid out by
/// simple word wrap, and only the lines on screen are painted.
class NotationView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit NotationView(const AppStyle& style, QWidget* parent = nullptr);

    /// Shows a game (rebuilds the text). The game must outlive the view or the next setGame().
    void setGame(const core::Game* game, core::NodeId current);
    /// Moves the highlight. Cheap: doesn't rebuild the text.
    void setCurrent(core::NodeId current);

    /// Two ways to show the moves: flowing text (the default), or rows (number, White, Black)
    /// with comments and variations as blocks in between, as En Croissant shows them.
    void setRows(bool rows);
    /// Pieces as symbols (♘f3) instead of letters (Nf3).
    void setFigurines(bool figurines);
    [[nodiscard]] bool rows() const noexcept { return m_rows; }
    /// A repertoire file: where it starts, marked with a flag (kNoNode: nowhere), and whether
    /// the right-click menu offers "Start Here".
    void setStart(core::NodeId start, bool offered);
    [[nodiscard]] core::NodeId start() const noexcept { return m_start; }

    /// The text as shown, one paragraph per line (for tests and copying).
    [[nodiscard]] QString plainText() const;
    /// Where a move is drawn, in viewport coordinates (empty if it isn't in the list).
    [[nodiscard]] QRect moveRect(core::NodeId id) const;

signals:
    void nodeActivated(kiebitz::core::NodeId id);
    void promoteRequested(kiebitz::core::NodeId id);
    void makeMainLineRequested(kiebitz::core::NodeId id);
    void deleteFromRequested(kiebitz::core::NodeId id);
    void deleteVariationRequested(kiebitz::core::NodeId id);
    void nagToggled(kiebitz::core::NodeId id, std::uint8_t nag);
    void nagsCleared(kiebitz::core::NodeId id);
    void commentEditRequested(kiebitz::core::NodeId id);
    /// "Starting Position": the file starts after this move (the root: at the very start).
    void startRequested(kiebitz::core::NodeId id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    /// How a word looks. Each kind has its own font and colour.
    enum class Kind : std::uint8_t {
        Number,
        MainMove,
        SideMove,
        Comment,
        Author, ///< who wrote the comment after it
        Bracket,
        Result,
        Hint,
    };
    static constexpr std::size_t kKinds = 8;
    /// How a word joins the one before it.
    enum class Join : std::uint8_t {
        Space, ///< a space; the line may break here
        Nbsp,  ///< a space, but never a break ("12." and its move)
        Touch, ///< no space and no break ("(" and the move after it)
    };
    struct Word {
        QString text;
        core::NodeId node = core::kNoNode; ///< the move it belongs to, for moves
        Kind kind = Kind::Comment;
        Join join = Join::Space;
        bool small = false; ///< in a variation paragraph (smaller font)
        bool flag = false;  ///< where the file starts: a flag before the move
        qreal x = 0;        ///< laid out: left edge, relative to the view's content
        qreal width = 0;
        int line = 0; ///< laid out: index into m_lines
    };
    /// How a paragraph is laid out.
    enum class Style : std::uint8_t {
        Flow, ///< words wrapped like text
        Row,  ///< rows view: number, White's move, Black's move in columns
        Note, ///< rows view: a comment between the rows
    };
    struct Paragraph {
        int first = 0; ///< words [first, end)
        int end = 0;
        bool variation = false;
        Style style = Style::Flow;
        bool blackOnly = false; ///< a row that starts with Black's move ("5 … Nf6")
    };
    struct Line {
        int top = 0; ///< in content coordinates
        int height = 0;
        int first = 0; ///< words [first, end)
        int end = 0;
        bool small = false;
        bool bar = false; ///< rows view: a variation block, with a line at its left
    };

    void rebuild();
    void layoutWords();
    void updateScrollBar();
    void updateFonts();
    void scrollToMove(core::NodeId id);
    [[nodiscard]] static int fontIndex(Kind kind, bool small) {
        return (static_cast<int>(kind) * 2) + (small ? 1 : 0);
    }
    [[nodiscard]] qreal widthOf(const Word& word);
    [[nodiscard]] int wordAt(QPoint viewportPoint) const;
    [[nodiscard]] core::NodeId nodeAt(QPoint viewportPoint) const;
    [[nodiscard]] QRectF wordRect(int index) const; ///< in viewport coordinates

    const AppStyle& m_style;
    const core::Game* m_game = nullptr;
    core::NodeId m_current = core::Game::root();

    std::vector<Word> m_words;
    std::vector<Paragraph> m_paragraphs;
    std::vector<Line> m_lines;
    std::unordered_map<core::NodeId, int> m_moveWord; ///< which word shows each move
    int m_contentHeight = 0;
    int m_laidOutWidth = -1;

    /// One font per kind and size, and the measured widths of the words in it.
    std::array<QFont, 2 * kKinds> m_fonts;
    std::array<QHash<QString, qreal>, 2 * kKinds> m_widths;
    std::array<qreal, 2> m_spaceWidth{}; ///< [normal, small]
    std::array<int, 2> m_lineHeight{};
    std::array<qreal, 2> m_ascent{};

    core::NodeId m_pressed = core::kNoNode; ///< a click goes to the move it started on
    int m_hovered = -1;                     ///< the move word under the mouse
    core::NodeId m_start = core::kNoNode;   ///< the move the file starts after (flagged)
    bool m_startOffered = false;            ///< "Start Here" in the menu (repertoire files)
    bool m_rows = false;                    ///< the rows view
    bool m_figurines = false;               ///< ♘f3 instead of Nf3
};

} // namespace kiebitz::app
