#include "NotationView.hpp"

#include "kiebitz/core/annotations.hpp"
#include "style/AppStyle.hpp"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>

#include <algorithm>
#include <array>
#include <functional>

namespace kiebitz::app {
namespace {

using core::NodeId;

QString html(const std::string& text) {
    return QString::fromStdString(text).toHtmlEscaped();
}

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
    : QTextBrowser(parent), m_style(style) {
    setObjectName(QStringLiteral("moveList"));
    setOpenLinks(false);
    setFocusPolicy(Qt::NoFocus); // keep the arrow keys for moving through the game
    setFrameShape(QFrame::NoFrame);
    connect(this, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        bool ok = false;
        const auto id = static_cast<NodeId>(url.fragment().toUInt(&ok));
        if (ok) {
            emit nodeActivated(id);
        }
    });
}

void NotationView::setGame(const core::Game* game, NodeId current) {
    m_game = game;
    m_current = current;
    render();
}

// ---- Rendering ---------------------------------------------------------------------------------

void NotationView::render() {
    const int scroll = verticalScrollBar()->value();
    m_ranges.clear();
    m_highlighted = core::kNoNode;
    if (m_game == nullptr) {
        clear();
        return;
    }
    const auto& c = m_style.colors();
    const core::Game& game = *m_game;
    QString text;

    const auto comment = [&](const std::string& value) {
        if (!value.empty()) {
            text += QStringLiteral("<span style='color:%1;font-style:italic'>%2</span> ")
                        .arg(c.muted.name(), html(value));
        }
    };
    const auto move = [&](NodeId id, bool forceNumber, bool mainLine) {
        const core::Node& node = game.node(id);
        comment(node.commentBefore);
        const bool white = game.mover(id) == core::Color::White;
        if (white || forceNumber || !node.commentBefore.empty()) {
            text += QStringLiteral("<span style='color:%1'>%2%3</span>&nbsp;")
                        .arg(c.muted.name())
                        .arg(game.moveNumber(id))
                        .arg(white ? QStringLiteral(".") : QStringLiteral("…"));
        }
        QString san = html(node.san);
        for (const auto nag : node.nags) {
            san += nagText(nag);
        }
        // The game itself stands out (heavier, full colour); alternatives are quieter.
        text += QStringLiteral(
                    "<a href='#%1' style='color:%2;font-weight:%3;text-decoration:none'>%4</a> ")
                    .arg(id)
                    .arg((mainLine ? c.text : c.muted).name(),
                         mainLine ? QStringLiteral("600") : QStringLiteral("400"), san);
        comment(node.comment);
        return !node.comment.empty(); // after a comment the next move shows its number
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
                    text += QStringLiteral(
                        "</p><p style='margin:5px 0 5px 20px;padding:0;font-size:15px'>");
                } else {
                    text += QStringLiteral("<span style='color:%1'>(</span>").arg(c.muted.name());
                }
                line(alt, depth + 1, move(alt, true, false));
                if (depth == 0) {
                    text += QStringLiteral("</p><p style='margin:0'>");
                } else {
                    if (text.endsWith(QLatin1Char(' '))) {
                        text.chop(1);
                    }
                    text += QStringLiteral("<span style='color:%1'>)</span> ").arg(c.muted.name());
                }
                number = true;
            }
        }
    };

    text += QStringLiteral("<p style='margin:0'>");
    comment(game.node(core::Game::root()).comment);
    line(core::Game::root(), 0, true);
    if (game.mainChild(core::Game::root()) == core::kNoNode) {
        text += QStringLiteral("<span style='color:%1'>%2</span>")
                    .arg(c.muted.name(), tr("Play a move on the board to start."));
    } else if (const auto result = game.header("Result"); result != "*" && !result.empty()) {
        text += QStringLiteral("<b style='color:%1'>%2</b>")
                    .arg(c.muted.name(), QString::fromStdString(std::string(result)));
    }
    text += QStringLiteral("</p>");
    text.replace(QStringLiteral("<p style='margin:0'></p>"), QString());

    setHtml(QStringLiteral("<div style='font-size:17px;line-height:175%'>%1</div>").arg(text));
    indexAnchors();
    verticalScrollBar()->setValue(scroll);
    setCurrent(m_current);
}

void NotationView::indexAnchors() {
    // Remember where each move's link is, so the highlight can move without re-rendering.
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QTextCharFormat format = fragment.charFormat();
            if (!format.isAnchor()) {
                continue;
            }
            bool ok = false;
            const auto id =
                static_cast<NodeId>(format.anchorHref().mid(1).toUInt(&ok)); // "#12" → 12
            if (ok) {
                m_ranges[id] = {fragment.position(), fragment.length()};
            }
        }
    }
}

void NotationView::setCurrent(NodeId current) {
    m_current = current;
    // Put back the previous move's own format…
    if (m_highlighted != core::kNoNode) {
        if (const auto it = m_ranges.find(m_highlighted); it != m_ranges.end()) {
            QTextCursor cursor(document());
            cursor.setPosition(it->second.start);
            cursor.setPosition(it->second.start + it->second.length, QTextCursor::KeepAnchor);
            cursor.setCharFormat(m_savedFormat);
        }
        m_highlighted = core::kNoNode;
    }
    // …and highlight the new one: accent text on a soft accent tint.
    const auto it = m_ranges.find(current);
    if (it == m_ranges.end()) {
        return;
    }
    QTextCursor cursor(document());
    cursor.setPosition(it->second.start);
    cursor.setPosition(it->second.start + it->second.length, QTextCursor::KeepAnchor);
    QTextCursor probe(document()); // the format inside the range, not before it
    probe.setPosition(it->second.start + 1);
    m_savedFormat = probe.charFormat();
    QTextCharFormat highlight;
    const auto& c = m_style.colors();
    QColor tint = c.accent;
    tint.setAlphaF(0.22F);
    highlight.setBackground(tint);
    highlight.setForeground(c.accent);
    highlight.setFontWeight(QFont::DemiBold);
    cursor.mergeCharFormat(highlight);
    m_highlighted = current;

    // Scroll so the move is visible (without moving a caret the user would see).
    QTextCursor visible(document());
    visible.setPosition(it->second.start);
    const QRect rect = cursorRect(visible);
    if (!viewport()->rect().adjusted(0, 8, 0, -8).contains(rect)) {
        verticalScrollBar()->setValue(verticalScrollBar()->value() + rect.center().y() -
                                      (viewport()->height() / 2));
    }
}

// ---- Context menu ------------------------------------------------------------------------------

NodeId NotationView::nodeAt(QPoint point) const {
    const QString href = anchorAt(point);
    bool ok = false;
    const auto id = static_cast<NodeId>(href.mid(1).toUInt(&ok));
    return ok ? id : core::kNoNode;
}

void NotationView::mouseDoubleClickEvent(QMouseEvent* event) {
    // Double-click a move to write a comment on it.
    if (const NodeId id = nodeAt(event->position().toPoint()); id != core::kNoNode) {
        emit nodeActivated(id);
        emit commentEditRequested(id);
        return;
    }
    QTextBrowser::mouseDoubleClickEvent(event);
}

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
