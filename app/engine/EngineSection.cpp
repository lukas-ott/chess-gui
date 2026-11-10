#include "EngineSection.hpp"

#include "CloudEval.hpp"
#include "UciEngine.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace chessgui::app {
namespace {

constexpr int kShownPlies = 14; ///< moves shown per line (the rest is cut off)

QLabel* label(const QString& text, const char* role) {
    auto* result = new QLabel(text);
    result->setProperty("role", role);
    return result;
}

/// "412 M", "38 k", "950".
QString compact(std::uint64_t value) {
    if (value >= 1'000'000'000) {
        return QStringLiteral("%1 G").arg(static_cast<double>(value) / 1e9, 0, 'f', 1);
    }
    if (value >= 1'000'000) {
        return QStringLiteral("%1 M").arg(static_cast<double>(value) / 1e6, 0, 'f', 1);
    }
    if (value >= 1'000) {
        return QStringLiteral("%1 k").arg(value / 1000);
    }
    return QString::number(value);
}

/// "1. e4 e5 2. Nf3" or "3… Nf6 4. d4", each move a link "#<line>-<ply>".
QString lineText(const core::Position& position, const std::vector<core::Move>& moves, int line,
                 const QColor& moveColor, const QColor& numberColor) {
    QString text;
    core::Position current = position;
    int number = position.fullmoveNumber();
    const auto shown = std::min<std::size_t>(moves.size(), kShownPlies);
    for (std::size_t ply = 0; ply < shown; ++ply) {
        const bool white = current.sideToMove() == core::Color::White;
        if (white || ply == 0) {
            text += QStringLiteral("<span style='color:%1'>%2%3</span>&nbsp;")
                        .arg(numberColor.name())
                        .arg(number)
                        .arg(white ? QStringLiteral(".") : QStringLiteral("…"));
        }
        // Qt's rich text has no "inherit": links need their colour spelled out.
        text += QStringLiteral("<a href='#%1-%2' style='text-decoration:none;color:%3'>%4</a> ")
                    .arg(line)
                    .arg(ply + 1)
                    .arg(moveColor.name(),
                         QString::fromStdString(current.san(moves[ply])).toHtmlEscaped());
        current.play(moves[ply]);
        if (!white) {
            ++number;
        }
    }
    if (moves.size() > shown) {
        text += QStringLiteral("…");
    }
    return text;
}

} // namespace

EngineSection::EngineSection(const QString& name, std::optional<EngineEntry> entry,
                             const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_name(name), m_entry(std::move(entry)), m_style(style) {
    setObjectName(QStringLiteral("engineSection"));
    setAccessibleName(name);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(4);

    // Header: Start/Stop, the engine's name, and what its lines are based on.
    auto* header = new QHBoxLayout;
    header->setSpacing(8);
    m_toggle = new QPushButton(tr("Start"));
    m_toggle->setObjectName(QStringLiteral("engineToggle"));
    m_toggle->setProperty("role", "engineToggle");
    m_toggle->setCheckable(true);
    m_toggle->setFocusPolicy(Qt::NoFocus); // Space belongs to the window's engine shortcut
    m_toggle->setFixedWidth(64);
    // toggled, not clicked: VoiceOver (accessibility) checks the button without "clicking" it.
    connect(m_toggle, &QPushButton::toggled, this, [this](bool on) { setRunning(on); });
    auto* icon = new QLabel;
    const auto& c = m_style.colors();
    icon->setPixmap(makeIcon(m_entry ? IconKind::Engine : IconKind::Cloud, c.muted, c.dimmed, 16)
                        .pixmap(16, 16));
    auto* title = label(name, "cardName");
    m_statusLabel = label(QString(), "engineStatus");
    m_statusLabel->setObjectName(QStringLiteral("engineStatus"));
    m_statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header->addWidget(m_toggle);
    header->addWidget(icon);
    header->addWidget(title);
    header->addWidget(m_statusLabel, 1);
    layout->addLayout(header);

    for (int i = 0; i < kMaxLines; ++i) {
        LineRow& row = m_rows[static_cast<std::size_t>(i)];
        row.row = new QWidget;
        auto* rowLayout = new QHBoxLayout(row.row);
        rowLayout->setContentsMargins(0, 1, 0, 1);
        rowLayout->setSpacing(8);
        row.score = label(QString(), "scorePill");
        row.score->setAlignment(Qt::AlignCenter);
        row.score->setFixedWidth(64);
        row.moves = label(QString(), "pvLine");
        row.moves->setObjectName(QStringLiteral("pvLine%1").arg(i + 1));
        row.moves->setTextFormat(Qt::RichText);
        row.moves->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        // Long lines are cut at the panel's edge instead of widening the window.
        row.moves->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        // Queued: playing the line rewrites this very label, which must not happen while it is
        // still handling the click.
        connect(
            row.moves, &QLabel::linkActivated, this,
            [this](const QString& link) {
                const QStringList parts = link.mid(1).split(QLatin1Char('-'));
                if (parts.size() == 2) {
                    playLine(parts[0].toInt(), parts[1].toInt());
                }
            },
            Qt::QueuedConnection);
        rowLayout->addWidget(row.score, 0, Qt::AlignTop);
        rowLayout->addWidget(row.moves, 1);
        row.row->hide();
        layout->addWidget(row.row);
    }
}

EngineSection::~EngineSection() = default;

void EngineSection::setRunning(bool running) {
    if (running == m_running) {
        return;
    }
    m_running = running;
    {
        const QSignalBlocker block(m_toggle);
        m_toggle->setChecked(running);
        m_toggle->setText(running ? tr("Stop") : tr("Start"));
    }
    if (running) {
        ensureSource();
        analyseCurrent();
    } else {
        if (m_source) {
            m_source->stop();
        }
        m_lines.clear();
        m_over.reset();
        m_status.clear();
        showLines();
    }
    emit changed();
}

void EngineSection::setPosition(std::optional<core::Position> position) {
    m_position = std::move(position);
    if (m_running) {
        analyseCurrent();
        emit changed();
    }
}

void EngineSection::setLineCount(int lines) {
    m_multiPv = lines;
    if (m_source) {
        m_source->setMultiPv(lines);
    }
    showLines();
    emit changed();
}

void EngineSection::setEntry(const EngineEntry& entry) {
    m_entry = entry;
    if (m_running) {
        setRunning(false);
        dropSource();
        setRunning(true);
    } else {
        dropSource();
    }
}

void EngineSection::analyseCurrent() {
    if (!m_source) {
        return;
    }
    m_lines.clear();
    m_over.reset();
    if (!m_position) {
        m_source->stop();
        m_status.clear();
        showLines();
        return;
    }
    const core::Position position = *m_position;
    if (position.isCheckmate() || position.isStalemate()) {
        m_source->stop();
        // Mated: the side to move lost; stalemate is a draw.
        m_over = engine::Score{engine::Score::Kind::Over, position.isCheckmate() ? -1 : 0}.forWhite(
            position.sideToMove());
        m_status = position.isCheckmate() ? tr("Checkmate") : tr("Stalemate");
        showLines();
        return;
    }
    m_status.clear();
    showLines();
    m_source->analyse(position);
}

void EngineSection::ensureSource() {
    if (m_source) {
        return;
    }
    if (m_entry) {
        m_source = std::make_unique<UciEngine>(*m_entry);
    } else {
        m_source = std::make_unique<CloudEval>();
    }
    m_source->setMultiPv(m_multiPv);
    connect(m_source.get(), &AnalysisSource::updated, this, &EngineSection::onUpdated);
    connect(m_source.get(), &AnalysisSource::statusChanged, this, &EngineSection::showStatus);
    connect(m_source.get(), &AnalysisSource::failed, this, [this](const QString& message) {
        // Out of the source's own signal before it is deleted.
        QTimer::singleShot(0, this, [this, message] {
            dropSource();
            setRunning(false);
            showStatus(message);
        });
    });
}

void EngineSection::dropSource() {
    if (m_source) {
        m_source->disconnect(this);
        m_source.reset(); // a UCI engine's process quits with it
    }
}

void EngineSection::onUpdated(const AnalysisUpdate& update) {
    if (!m_running || !m_position || update.position.hash() != m_position->hash()) {
        return; // for a position that is no longer on the board
    }
    m_lines = update.lines;
    showLines();
    emit changed();
}

std::vector<core::Move> EngineSection::bestMoves() const {
    std::vector<core::Move> moves;
    if (!m_position) {
        return moves;
    }
    for (std::size_t i = 0; i < m_lines.size() && std::cmp_less(i, m_multiPv); ++i) {
        const auto first = engine::pvMoves(*m_position, {m_lines[i].pv.front()});
        if (!first.empty()) {
            moves.push_back(first.front());
        }
    }
    return moves;
}

std::optional<engine::Score> EngineSection::score() const {
    if (m_over) {
        return m_over;
    }
    if (!m_position || m_lines.empty()) {
        return std::nullopt;
    }
    return m_lines.front().score.forWhite(m_position->sideToMove());
}

void EngineSection::showLines() {
    const auto& c = m_style.colors();
    for (int i = 0; i < kMaxLines; ++i) {
        const LineRow& row = m_rows[static_cast<std::size_t>(i)];
        if (!m_running || !m_position || std::cmp_greater_equal(i, m_lines.size()) ||
            i >= m_multiPv) {
            row.row->hide();
            continue;
        }
        const engine::Info& info = m_lines[static_cast<std::size_t>(i)];
        const engine::Score white = info.score.forWhite(m_position->sideToMove());
        row.score->setText(QString::fromStdString(engine::formatScore(white)));
        const char* side = white.value >= 0 ? "white" : "black";
        if (row.score->property("side").toString() != QLatin1String(side)) {
            row.score->setProperty("side", side);
            row.score->style()->unpolish(row.score);
            row.score->style()->polish(row.score);
        }
        row.moves->setText(
            lineText(*m_position, engine::pvMoves(*m_position, info.pv), i + 1, c.text, c.muted));
        row.row->show();
    }
    // What the best line is based on.
    if (m_lines.empty()) {
        m_statusLabel->setText(m_status.isEmpty() && m_running && m_position ? tr("Thinking…")
                                                                             : m_status);
        return;
    }
    const engine::Info& best = m_lines.front();
    QStringList parts{tr("Depth %1").arg(best.depth)};
    if (best.nps > 0) {
        parts << tr("%1 nodes/s").arg(compact(best.nps));
    }
    if (best.nodes > 0) {
        parts << tr("%1 nodes").arg(compact(best.nodes));
    }
    m_statusLabel->setText(parts.join(QStringLiteral("  ·  ")));
}

void EngineSection::showStatus(const QString& text) {
    m_status = text;
    if (m_lines.empty()) {
        m_statusLabel->setText(text);
    }
}

UciEngine* EngineSection::uciEngine() const {
    return qobject_cast<UciEngine*>(m_source.get());
}

void EngineSection::playLine(int line, int plies) {
    if (!m_position || line < 1 || std::cmp_greater(line, m_lines.size()) || plies < 1) {
        return;
    }
    auto moves = engine::pvMoves(*m_position, m_lines[static_cast<std::size_t>(line - 1)].pv);
    if (moves.size() > static_cast<std::size_t>(plies)) {
        moves.resize(static_cast<std::size_t>(plies));
    }
    if (!moves.empty()) {
        emit playRequested(moves);
    }
}

} // namespace chessgui::app
