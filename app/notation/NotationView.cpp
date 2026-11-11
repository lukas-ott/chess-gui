#include "NotationView.hpp"

#include "kiebitz/core/annotations.hpp"
#include "style/AppStyle.hpp"

#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace kiebitz::app {
namespace {

using core::NodeId;

constexpr int kMargin = 6;           ///< around the text
constexpr int kVariationIndent = 20; ///< first-level variations are indented
constexpr int kVariationGap = 5;     ///< and have a little air above and below
constexpr int kMainPixels = 17;
constexpr int kVariationPixels = 15;
constexpr double kLineSpacing = 1.75; ///< generous, so moves are easy to click

QString nagText(std::uint8_t nag) {
    const auto symbol = core::nagSymbol(nag);
    return QString::fromUtf8(symbol.data(), static_cast<qsizetype>(symbol.size()));
}

/// NAGs offered in the menu: how good the move is, and who stands better.
struct NagChoice {
    std::uint8_t nag;
    const char* label;
};
constexpr std::array<NagChoice, 6> kMoveNags{{{1, "Good move"},
                                              {2, "Mistake"},
                                              {3, "Brilliant move"},
                                              {4, "Blunder"},
                                              {5, "Interesting move"},
                                              {6, "Dubious move"}}};
constexpr std::array<NagChoice, 8> kPositionNags{{{10, "Equal"},
                                                  {13, "Unclear"},
                                                  {14, "White is slightly better"},
                                                  {15, "Black is slightly better"},
                                                  {16, "White is better"},
                                                  {17, "Black is better"},
                                                  {18, "White is winning"},
                                                  {19, "Black is winning"}}};

} // namespace

NotationView::NotationView(const AppStyle& style, QWidget* parent)
    : QAbstractScrollArea(parent), m_style(style) {
    setObjectName(QStringLiteral("moveList"));
    setFocusPolicy(Qt::NoFocus); // keep the arrow keys for moving through the game
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setMouseTracking(true); // the hand cursor over moves
    updateFonts();
}

void NotationView::setGame(const core::Game* game, NodeId current) {
    m_game = game;
    m_current = current;
    rebuild();
}

// ---- Words -------------------------------------------------------------------------------------

void NotationView::updateFonts() {
    for (const bool small : {false, true}) {
        for (std::size_t k = 0; k < kKinds; ++k) {
            const auto kind = static_cast<Kind>(k);
            QFont f = font();
            int pixels = small ? kVariationPixels : kMainPixels;
            if (kind == Kind::Clock) {
                pixels -= 4;
            } else if (kind == Kind::Author) {
                pixels -= 2;
            }
            f.setPixelSize(pixels);
            QFont::Weight weight = QFont::Normal;
            if (kind == Kind::MainMove || kind == Kind::Author) {
                weight = QFont::DemiBold;
            } else if (kind == Kind::Result) {
                weight = QFont::Bold;
            }
            f.setWeight(weight);
            f.setItalic(kind == Kind::Comment);
            m_fonts[static_cast<std::size_t>(fontIndex(kind, small))] = f;
            m_widths[static_cast<std::size_t>(fontIndex(kind, small))].clear();
        }
        const QFontMetricsF metrics(
            m_fonts[static_cast<std::size_t>(fontIndex(Kind::Number, small))]);
        const auto size = static_cast<std::size_t>(small ? 1 : 0);
        m_spaceWidth[size] = metrics.horizontalAdvance(QLatin1Char(' '));
        m_lineHeight[size] = static_cast<int>(std::lround(metrics.height() * kLineSpacing));
        m_ascent[size] =
            static_cast<qreal>(metrics.ascent() + ((metrics.height() * (kLineSpacing - 1)) / 2));
    }
}

qreal NotationView::widthOf(const Word& word) {
    auto& cache = m_widths[static_cast<std::size_t>(fontIndex(word.kind, word.small))];
    if (const auto it = cache.constFind(word.text); it != cache.constEnd()) {
        return *it;
    }
    const QFontMetricsF metrics(
        m_fonts[static_cast<std::size_t>(fontIndex(word.kind, word.small))]);
    const qreal width = metrics.horizontalAdvance(word.text);
    cache.insert(word.text, width);
    return width;
}

void NotationView::rebuild() {
    const int scroll = verticalScrollBar()->value();
    m_words.clear();
    m_paragraphs.clear();
    m_moveWord.clear();
    m_hovered = -1;
    if (m_game == nullptr) {
        layoutWords();
        viewport()->update();
        return;
    }
    const core::Game& game = *m_game;
    m_words.reserve(game.nodeCount() * 3);
    m_moveWord.reserve(game.nodeCount());

    // The writer remembers how the next word joins the last one.
    bool variation = false;
    bool spaceOwed = false;
    bool nbspOwed = false;
    m_paragraphs.push_back({0, 0, false});
    const auto add = [&](QString text, Kind kind, NodeId node = core::kNoNode) {
        Word word;
        word.text = std::move(text);
        word.kind = kind;
        word.node = node;
        word.small = variation;
        word.join = Join::Touch;
        if (nbspOwed) {
            word.join = Join::Nbsp;
        } else if (spaceOwed) {
            word.join = Join::Space;
        }
        word.width = widthOf(word);
        m_words.push_back(std::move(word));
        spaceOwed = true;
        nbspOwed = false;
    };
    // A new paragraph, unless the current one is still empty (then it just changes style).
    const auto paragraph = [&](bool isVariation, Style style = Style::Flow) {
        Paragraph& last = m_paragraphs.back();
        last.end = static_cast<int>(m_words.size());
        if (last.end > last.first) {
            m_paragraphs.push_back({last.end, last.end, isVariation, style, false});
        } else {
            last.variation = isVariation;
            last.style = style;
            last.blackOnly = false;
        }
        variation = isVariation;
        spaceOwed = false;
        nbspOwed = false;
    };
    // A comment as people read it: the clock right after the move, then who wrote it (from a
    // Lichess study's [%anno]), then the text, without the [%...] commands tools put in. True
    // if it showed some text (then the next move gets its number again).
    const auto comment = [&](const std::string& value) {
        if (value.empty()) {
            return false;
        }
        const core::CommentDisplay shown = core::displayComment(value);
        if (!shown.clock.empty()) {
            nbspOwed = spaceOwed; // stays with its move
            add(QString::fromStdString(shown.clock), Kind::Clock);
        }
        if (!shown.author.empty()) {
            add(QString::fromStdString(shown.author) + QLatin1Char(':'), Kind::Author);
        }
        for (const QString& part :
             QString::fromStdString(shown.text).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            add(part, Kind::Comment);
        }
        return !shown.text.empty() || !shown.author.empty();
    };
    const auto move = [&](NodeId id, bool forceNumber, bool mainLine) {
        const core::Node& node = game.node(id);
        const bool textBefore = comment(node.commentBefore);
        const bool white = game.mover(id) == core::Color::White;
        if (white || forceNumber || textBefore) {
            add(QString::number(game.moveNumber(id)) +
                    (white ? QStringLiteral(".") : QStringLiteral("…")),
                Kind::Number);
            nbspOwed = true;
        }
        QString san = QString::fromStdString(node.san);
        for (const auto nag : node.nags) {
            san += nagText(nag);
        }
        // The game itself stands out (heavier, full colour); alternatives are quieter.
        m_moveWord[id] = static_cast<int>(m_words.size());
        add(std::move(san), mainLine ? Kind::MainMove : Kind::SideMove, id);
        return comment(node.comment); // after a comment the next move shows its number
    };

    // Writes the moves after `parent`. At depth 0 (the main line) each set of alternatives
    // gets its own indented paragraph; deeper alternatives go inline in brackets.
    std::function<void(NodeId, int, bool)> line = [&](NodeId parent, int depth, bool number) {
        for (NodeId node = game.mainChild(parent); node != core::kNoNode;
             node = game.mainChild(node)) {
            number = move(node, number, depth == 0);
            for (NodeId alt = game.nextSibling(node); alt != core::kNoNode;
                 alt = game.nextSibling(alt)) {
                if (depth == 0) {
                    paragraph(true);
                } else {
                    add(QStringLiteral("("), Kind::Bracket);
                    spaceOwed = false;
                }
                line(alt, depth + 1, move(alt, true, false));
                if (depth == 0) {
                    paragraph(false);
                } else {
                    spaceOwed = false;
                    add(QStringLiteral(")"), Kind::Bracket);
                }
                number = true;
            }
        }
    };

    // Rows: the main line two moves to a row, its comments and variations as blocks in between.
    const auto rowsView = [&] {
        const auto note = [&](const std::string& value) { // a comment as its own paragraph
            const core::CommentDisplay shown = core::displayComment(value);
            if (shown.text.empty() && shown.author.empty()) {
                return false;
            }
            paragraph(false, Style::Note);
            comment(value);
            return true;
        };
        note(game.node(core::Game::root()).comment);
        bool rowOpen = false;
        for (NodeId node = game.mainChild(core::Game::root()); node != core::kNoNode;
             node = game.mainChild(node)) {
            const core::Node& n = game.node(node);
            const bool white = game.mover(node) == core::Color::White;
            if (note(n.commentBefore)) {
                rowOpen = false;
            }
            if (white || !rowOpen) {
                paragraph(false, Style::Row);
                add(QString::number(game.moveNumber(node)), Kind::Number);
                if (!white) {
                    add(QStringLiteral("…"), Kind::Number);
                    m_paragraphs.back().blackOnly = true;
                }
                rowOpen = true;
            }
            QString san = QString::fromStdString(n.san);
            for (const auto nag : n.nags) {
                san += nagText(nag);
            }
            m_moveWord[node] = static_cast<int>(m_words.size());
            add(std::move(san), Kind::MainMove, node);
            if (note(n.comment)) {
                rowOpen = false;
            }
            for (NodeId alt = game.nextSibling(node); alt != core::kNoNode;
                 alt = game.nextSibling(alt)) {
                paragraph(true);
                line(alt, 1, move(alt, true, false));
                rowOpen = false;
            }
            if (!white) {
                rowOpen = false; // Black's move ends the row
            }
        }
        paragraph(false, Style::Note);
    };

    if (m_rows) {
        rowsView();
    } else {
        comment(game.node(core::Game::root()).comment);
        line(core::Game::root(), 0, true);
    }
    if (game.mainChild(core::Game::root()) == core::kNoNode) {
        for (const QString& part :
             tr("Play a move on the board to start.").split(QLatin1Char(' '))) {
            add(part, Kind::Hint);
        }
    } else if (const auto result = game.header("Result"); result != "*" && !result.empty()) {
        add(QString::fromStdString(std::string(result)), Kind::Result);
    }
    m_paragraphs.back().end = static_cast<int>(m_words.size());
    if (m_paragraphs.size() > 1 && m_paragraphs.back().first == m_paragraphs.back().end) {
        m_paragraphs.pop_back(); // no empty paragraph at the end
    }

    layoutWords();
    verticalScrollBar()->setValue(scroll);
    setCurrent(m_current);
}

// ---- Layout ------------------------------------------------------------------------------------

void NotationView::layoutWords() {
    const int width = viewport()->width();
    m_laidOutWidth = width;
    m_lines.clear();
    int y = kMargin;
    for (const Paragraph& p : m_paragraphs) {
        if (p.first == p.end) {
            continue;
        }
        const auto size = static_cast<std::size_t>(p.variation ? 1 : 0);
        const int height = m_lineHeight[size];
        const qreal space = m_spaceWidth[size];
        if (p.style == Style::Row) {
            // Columns: the number, then White's and Black's moves sharing the rest.
            const qreal numberColumn = 40;
            const qreal whiteX = kMargin + numberColumn;
            const qreal blackX =
                whiteX +
                (std::max(120.0, static_cast<qreal>(width - (2 * kMargin)) - numberColumn) / 2);
            const std::array<qreal, 3> columns{static_cast<qreal>(kMargin), whiteX, blackX};
            for (int i = p.first; i < p.end; ++i) {
                Word& word = m_words[static_cast<std::size_t>(i)];
                word.x = columns[static_cast<std::size_t>(std::min(i - p.first, 2))];
                word.line = static_cast<int>(m_lines.size());
            }
            m_lines.push_back(Line{y, height, p.first, p.end, false, false});
            y += height;
            continue;
        }
        const bool bar = m_rows && p.variation;
        const auto left =
            static_cast<qreal>(kMargin + (p.variation ? kVariationIndent : 0) + (bar ? 6 : 0));
        const qreal room = std::max(40.0, static_cast<qreal>(width - kMargin) - left);
        if (p.variation) {
            y += kVariationGap;
        }
        Line current{y, height, p.first, p.first, p.variation, bar};
        qreal x = 0;
        for (int i = p.first; i < p.end; ++i) {
            Word& word = m_words[static_cast<std::size_t>(i)];
            if (i > current.first) {
                const qreal gap = word.join == Join::Touch ? 0 : space;
                if (word.join == Join::Space) {
                    // Break before a word if it (with the words glued to it) doesn't fit.
                    qreal group = word.width;
                    for (int j = i + 1; j < p.end; ++j) {
                        const Word& next = m_words[static_cast<std::size_t>(j)];
                        if (next.join == Join::Space) {
                            break;
                        }
                        group += (next.join == Join::Nbsp ? space : 0) + next.width;
                    }
                    if (x + gap + group > room) {
                        current.end = i;
                        m_lines.push_back(current);
                        y += height;
                        current = Line{y, height, i, i, p.variation, bar};
                        x = 0;
                    } else {
                        x += gap;
                    }
                } else {
                    x += gap;
                }
            }
            word.x = left + x;
            word.line = static_cast<int>(m_lines.size());
            x += word.width;
        }
        current.end = p.end;
        m_lines.push_back(current);
        y += height;
        if (p.variation) {
            y += kVariationGap;
        }
    }
    m_contentHeight = y + kMargin;
    updateScrollBar();
}

void NotationView::updateScrollBar() {
    QScrollBar* bar = verticalScrollBar();
    bar->setRange(0, std::max(0, m_contentHeight - viewport()->height()));
    bar->setPageStep(viewport()->height());
    bar->setSingleStep(m_lineHeight[0]);
}

QRectF NotationView::wordRect(int index) const {
    const Word& word = m_words[static_cast<std::size_t>(index)];
    const Line& line = m_lines[static_cast<std::size_t>(word.line)];
    return {word.x, static_cast<qreal>(line.top - verticalScrollBar()->value()), word.width,
            static_cast<qreal>(line.height)};
}

QRect NotationView::moveRect(NodeId id) const {
    const auto it = m_moveWord.find(id);
    return it == m_moveWord.end() ? QRect() : wordRect(it->second).toAlignedRect();
}

int NotationView::wordAt(QPoint point) const {
    const int y = point.y() + verticalScrollBar()->value();
    const auto line = std::upper_bound(m_lines.begin(), m_lines.end(), y,
                                       [](int value, const Line& l) { return value < l.top; });
    if (line == m_lines.begin()) {
        return -1;
    }
    const Line& l = *std::prev(line);
    if (y >= l.top + l.height) {
        return -1; // in the gap between paragraphs
    }
    const auto x = static_cast<qreal>(point.x());
    for (int i = l.first; i < l.end; ++i) {
        const Word& word = m_words[static_cast<std::size_t>(i)];
        if (x >= word.x - 2 && x < word.x + word.width + 2) {
            return i;
        }
    }
    return -1;
}

NodeId NotationView::nodeAt(QPoint point) const {
    const int index = wordAt(point);
    return index < 0 ? core::kNoNode : m_words[static_cast<std::size_t>(index)].node;
}

QString NotationView::plainText() const {
    QStringList paragraphs;
    for (const Paragraph& p : m_paragraphs) {
        QString text;
        for (int i = p.first; i < p.end; ++i) {
            const Word& word = m_words[static_cast<std::size_t>(i)];
            if (i > p.first) {
                if (word.join == Join::Space) {
                    text += QLatin1Char(' ');
                } else if (word.join == Join::Nbsp) {
                    text += QChar::Nbsp;
                }
            }
            text += word.text;
        }
        paragraphs << text;
    }
    return paragraphs.join(QLatin1Char('\n'));
}

// ---- Highlight and painting --------------------------------------------------------------------

void NotationView::setRows(bool rows) {
    if (rows != m_rows) {
        m_rows = rows;
        rebuild();
    }
}

void NotationView::setCurrent(NodeId current) {
    m_current = current;
    scrollToMove(current);
    viewport()->update();
}

void NotationView::scrollToMove(NodeId id) {
    const QRect rect = moveRect(id);
    if (rect.isEmpty() || viewport()->rect().adjusted(0, 8, 0, -8).contains(rect)) {
        return;
    }
    verticalScrollBar()->setValue(verticalScrollBar()->value() + rect.center().y() -
                                  (viewport()->height() / 2));
}

void NotationView::paintEvent(QPaintEvent* event) {
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    const auto& c = m_style.colors();
    const int scroll = verticalScrollBar()->value();
    const QRect area = event->rect();
    // Only the lines on screen.
    auto line = std::upper_bound(m_lines.begin(), m_lines.end(), area.top() + scroll,
                                 [](int value, const Line& l) { return value < l.top + l.height; });
    const auto current = m_moveWord.find(m_current);
    const int currentWord = current == m_moveWord.end() ? -1 : current->second;
    QColor tint = c.accent;
    tint.setAlphaF(0.22F);
    for (; line != m_lines.end() && line->top - scroll < area.bottom(); ++line) {
        const auto size = static_cast<std::size_t>(line->small ? 1 : 0);
        if (line->bar) { // rows view: a variation block's line at its left
            const qreal x = kMargin + kVariationIndent - 6;
            painter.fillRect(QRectF(x, line->top - scroll, 2, line->height), c.border);
        }
        const qreal baseline = static_cast<qreal>(line->top - scroll) + m_ascent[size];
        for (int i = line->first; i < line->end; ++i) {
            const Word& word = m_words[static_cast<std::size_t>(i)];
            QColor color = c.muted;
            if (word.kind == Kind::MainMove) {
                color = c.text;
            } else if (word.kind == Kind::Clock) {
                color = c.dimmed;
            }
            if (i == currentWord) { // accent text on a soft accent tint
                const QFontMetricsF metrics(
                    m_fonts[static_cast<std::size_t>(fontIndex(word.kind, word.small))]);
                const QRectF box(word.x - 3, baseline - metrics.ascent() - 2, word.width + 6,
                                 metrics.height() + 4);
                QPainterPath path;
                path.addRoundedRect(box, 5, 5);
                painter.fillPath(path, tint);
                color = c.accent;
            } else if (i == m_hovered) {
                color = c.text;
            }
            painter.setFont(m_fonts[static_cast<std::size_t>(fontIndex(word.kind, word.small))]);
            painter.setPen(color);
            painter.drawText(QPointF(word.x, baseline), word.text);
        }
    }
}

void NotationView::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    if (viewport()->width() != m_laidOutWidth) {
        layoutWords();
        scrollToMove(m_current);
    } else {
        updateScrollBar();
    }
}

void NotationView::changeEvent(QEvent* event) {
    QAbstractScrollArea::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        updateFonts();
        for (Word& word : m_words) {
            word.width = widthOf(word);
        }
        layoutWords();
        viewport()->update();
    }
}

// ---- Mouse -------------------------------------------------------------------------------------

void NotationView::mousePressEvent(QMouseEvent* event) {
    m_pressed =
        event->button() == Qt::LeftButton ? nodeAt(event->position().toPoint()) : core::kNoNode;
    QAbstractScrollArea::mousePressEvent(event);
}

void NotationView::mouseReleaseEvent(QMouseEvent* event) {
    // A click selects the move it started and ended on.
    if (event->button() == Qt::LeftButton && m_pressed != core::kNoNode &&
        nodeAt(event->position().toPoint()) == m_pressed) {
        emit nodeActivated(m_pressed);
    }
    m_pressed = core::kNoNode;
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void NotationView::mouseMoveEvent(QMouseEvent* event) {
    int hovered = wordAt(event->position().toPoint());
    if (hovered >= 0 && m_words[static_cast<std::size_t>(hovered)].node == core::kNoNode) {
        hovered = -1;
    }
    if (hovered != m_hovered) {
        m_hovered = hovered;
        viewport()->setCursor(hovered >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        viewport()->update();
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void NotationView::leaveEvent(QEvent* event) {
    if (m_hovered >= 0) {
        m_hovered = -1;
        viewport()->update();
    }
    QAbstractScrollArea::leaveEvent(event);
}

void NotationView::mouseDoubleClickEvent(QMouseEvent* event) {
    // Double-click a move to write a comment on it.
    if (const NodeId id = nodeAt(event->position().toPoint()); id != core::kNoNode) {
        emit nodeActivated(id);
        emit commentEditRequested(id);
        return;
    }
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}

// ---- Context menu ------------------------------------------------------------------------------

void NotationView::contextMenuEvent(QContextMenuEvent* event) {
    if (m_game == nullptr) {
        return;
    }
    NodeId id = nodeAt(event->pos());
    if (id == core::kNoNode) {
        id = m_current; // right-click on empty space: act on the current move
    }
    if (id == core::Game::root()) {
        return;
    }
    emit nodeActivated(id); // act on what the user sees selected
    const core::Game& game = *m_game;
    const bool first = game.mainChild(game.parent(id)) == id;
    const bool mainLine = game.isMainLine(id);
    const auto& nags = game.node(id).nags;

    QMenu menu(this);
    auto* promote =
        menu.addAction(tr("Promote Variation"), this, [this, id] { emit promoteRequested(id); });
    promote->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up));
    promote->setEnabled(!first);
    menu.addAction(tr("Make Main Line"), this, [this, id] { emit makeMainLineRequested(id); })
        ->setEnabled(!mainLine);
    menu.addSeparator();
    auto* deleteFrom =
        menu.addAction(tr("Delete From Here"), this, [this, id] { emit deleteFromRequested(id); });
    deleteFrom->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Backspace));
    menu.addAction(tr("Delete Variation"), this, [this, id] { emit deleteVariationRequested(id); })
        ->setEnabled(!mainLine);
    menu.addSeparator();

    const auto addNags = [&](QMenu* sub, const auto& choices) {
        for (const auto& choice : choices) {
            auto* action =
                sub->addAction(QStringLiteral("%1\t%2").arg(tr(choice.label), nagText(choice.nag)));
            action->setCheckable(true);
            action->setChecked(std::find(nags.begin(), nags.end(), choice.nag) != nags.end());
            const auto nag = choice.nag;
            connect(action, &QAction::triggered, this,
                    [this, id, nag] { emit nagToggled(id, nag); });
        }
    };
    addNags(menu.addMenu(tr("Move")), kMoveNags);
    addNags(menu.addMenu(tr("Position")), kPositionNags);
    menu.addAction(tr("Clear Annotations"), this, [this, id] { emit nagsCleared(id); })
        ->setEnabled(!nags.empty());
    menu.addSeparator();
    menu.addAction(tr("Edit Comment…"), this, [this, id] { emit commentEditRequested(id); });
    menu.exec(event->globalPos());
}

} // namespace kiebitz::app
