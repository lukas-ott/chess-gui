#include "ExplorerPanel.hpp"

#include "databases/DatabaseManager.hpp"
#include "repertoire/RepertoireStore.hpp"
#include "style/AppStyle.hpp"

#include <QComboBox>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <array>
#include <tuple>

namespace kiebitz::app {

/// A bar in three parts: White won, drawn, Black won, with their shares written in when there is
/// room (as Lichess and En Croissant show results).
class ResultBar : public QWidget {
public:
    explicit ResultBar(const AppStyle& style, int height = 18, QWidget* parent = nullptr)
        : QWidget(parent), m_style(style) {
        setMinimumWidth(110);
        setFixedHeight(height);
    }
    void setResults(std::uint64_t white, std::uint64_t draws, std::uint64_t black) {
        m_counts = {white, draws, black};
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const std::uint64_t total = m_counts[0] + m_counts[1] + m_counts[2];
        const QRectF area = QRectF(rect()).adjusted(0, 1, 0, -1);
        QPainterPath clip;
        clip.addRoundedRect(area, area.height() / 2.5, area.height() / 2.5);
        painter.setClipPath(clip);
        const auto& c = m_style.colors();
        if (total == 0) {
            painter.fillRect(area, c.raised);
            return;
        }
        const std::array<QColor, 3> colors{c.whiteWins, c.draws, c.blackWins};
        const std::array<QColor, 3> texts{c.blackWins, c.whiteWins, c.whiteWins};
        QFont font = painter.font();
        font.setPixelSize(height() <= 14 ? 9 : 10);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        double x = area.left();
        for (std::size_t k = 0; k < 3; ++k) {
            const double share = static_cast<double>(m_counts[k]) / static_cast<double>(total);
            const QRectF part(x, area.top(), share * area.width(), area.height());
            painter.fillRect(part, colors[k]);
            const QString text = QStringLiteral("%1%").arg(qRound(share * 100));
            if (part.width() > painter.fontMetrics().horizontalAdvance(text) + 6) {
                painter.setPen(texts[k]);
                painter.drawText(part, Qt::AlignCenter, text);
            }
            x += part.width();
        }
        // A fine outline, so the dark (Black) part stays visible on a dark card.
        painter.setClipping(false);
        painter.setPen(QPen(c.border, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(area.adjusted(0.5, 0.5, -0.5, -0.5), area.height() / 2.5,
                                area.height() / 2.5);
    }

private:
    const AppStyle& m_style;
    std::array<std::uint64_t, 3> m_counts{};
};

namespace {

constexpr auto kSourceKey = "explorer/source";
constexpr int kMoveRole = Qt::UserRole;     ///< a move row: its move (UCI)
constexpr int kGameRole = Qt::UserRole + 1; ///< a game row: its id

QLabel* label(const QString& text, const char* role) {
    auto* result = new QLabel(text);
    result->setProperty("role", role);
    result->setWordWrap(true);
    return result;
}

QString number(std::uint64_t value) {
    return QLocale().toString(static_cast<qulonglong>(value));
}

QString ratings(int white, int black) {
    if (white == 0 && black == 0) {
        return {};
    }
    const auto one = [](int value) {
        return value > 0 ? QString::number(value) : QStringLiteral("–");
    };
    return QStringLiteral("%1 – %2").arg(one(white), one(black));
}

QString resultText(gamedb::GameResult result) {
    switch (result) {
    case gamedb::GameResult::WhiteWins:
        return QStringLiteral("1-0");
    case gamedb::GameResult::BlackWins:
        return QStringLiteral("0-1");
    case gamedb::GameResult::Draw:
        return QStringLiteral("½-½");
    case gamedb::GameResult::Unknown:
        break;
    }
    return QStringLiteral("*");
}

/// What the explorer found for a position.
struct Answer {
    gamedb::Explored explored;
    std::vector<gamedb::GameInfo> games;
};

} // namespace

ExplorerPanel::ExplorerPanel(DatabaseManager& databases, RepertoireStore& repertoires,
                             const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_databases(databases), m_repertoires(repertoires), m_style(style) {
    setObjectName(QStringLiteral("explorerPanel"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 10);
    m_pages = new QStackedWidget;
    outer->addWidget(m_pages);

    // 0: no databases yet.
    auto* none = new QWidget;
    auto* noneBox = new QVBoxLayout(none);
    noneBox->setContentsMargins(0, 0, 0, 0);
    noneBox->addWidget(label(tr("No databases yet"), "emptyTitle"));
    noneBox->addWidget(label(tr("Add a database of games (Lumbra's Gigabase, Caissabase or your "
                                "own) to see what is played here and how it scores."),
                             "emptyHint"));
    auto* open = new QPushButton(tr("Open Databases"));
    open->setProperty("role", "secondary");
    connect(open, &QPushButton::clicked, this, &ExplorerPanel::showDatabasesRequested);
    auto* openRow = new QHBoxLayout;
    openRow->addWidget(open);
    openRow->addStretch();
    noneBox->addSpacing(6);
    noneBox->addLayout(openRow);
    noneBox->addStretch();
    m_pages->addWidget(none);

    // 1: the explorer.
    auto* page = new QWidget;
    auto* box = new QVBoxLayout(page);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(6);
    m_source = new QComboBox;
    m_source->setObjectName(QStringLiteral("explorerSource"));
    m_source->setFocusPolicy(Qt::NoFocus);
    connect(m_source, &QComboBox::activated, this, [this] {
        QSettings().setValue(QString::fromLatin1(kSourceKey), m_source->currentText());
        refresh();
    });
    m_summary = label(QString(), "emptyHint");
    m_summary->setObjectName(QStringLiteral("explorerSummary"));
    m_summary->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_total = new ResultBar(m_style, 14);
    // Moves | Top Games: one list at a time, so the tab's height goes to it.
    auto* views = new QWidget;
    views->setProperty("role", "segmented");
    auto* viewsRow = new QHBoxLayout(views);
    viewsRow->setContentsMargins(2, 2, 2, 2);
    viewsRow->setSpacing(2);
    auto* lists = new QStackedWidget;
    for (const auto& [index, text, name] : {std::tuple{0, tr("Moves"), "explorerShowMoves"},
                                            std::tuple{1, tr("Top Games"), "explorerShowGames"}}) {
        auto* button = new QPushButton(text);
        button->setObjectName(QString::fromLatin1(name));
        button->setProperty("role", "segment");
        button->setCheckable(true);
        button->setChecked(index == 0);
        button->setAutoExclusive(true);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, lists,
                [lists, index] { lists->setCurrentIndex(index); });
        viewsRow->addWidget(button);
    }
    auto* top = new QHBoxLayout;
    top->setSpacing(8);
    top->addWidget(m_source);
    top->addWidget(views);
    top->addWidget(m_summary, 1);
    box->addLayout(top);
    box->addWidget(m_total);

    m_moves = new QTreeWidget;
    m_moves->setObjectName(QStringLiteral("explorerMoves"));
    m_moves->setColumnCount(5);
    m_moves->setHeaderLabels(
        {tr("MOVE"), tr("GAMES"), QStringLiteral("%"), tr("RESULTS"), tr("RATING")});
    m_moves->setRootIsDecorated(false);
    m_moves->setUniformRowHeights(true);
    m_moves->setFrameShape(QFrame::NoFrame);
    m_moves->setFocusPolicy(Qt::NoFocus);
    m_moves->header()->setStretchLastSection(false);
    m_moves->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_moves->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_moves->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_moves->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_moves->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_moves->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_moves->headerItem()->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    m_moves->headerItem()->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    connect(m_moves, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        if (const auto move =
                core::Move::fromUci(item->data(0, kMoveRole).toString().toStdString())) {
            emit movePlayed(*move);
        }
    });
    lists->addWidget(m_moves);

    m_games = new QTreeWidget;
    m_games->setObjectName(QStringLiteral("explorerGames"));
    m_games->setColumnCount(4);
    m_games->setHeaderHidden(true);
    m_games->setRootIsDecorated(false);
    m_games->setUniformRowHeights(true);
    m_games->setFrameShape(QFrame::NoFrame);
    m_games->header()->setStretchLastSection(false);
    m_games->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_games->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_games->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_games->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(
        m_games, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) { // opens it
            const auto id = item->data(0, kGameRole).toLongLong();
            for (const InstalledDatabase& database : m_databases.databases()) {
                if (QString::fromStdString(database.db->info().name) == m_source->currentText()) {
                    if (const auto game = database.db->game(id)) {
                        emit openGameRequested(*game);
                    }
                }
            }
        });
    lists->addWidget(m_games);
    box->addWidget(lists, 1);
    m_pages->addWidget(page);

    // 2: a game from a set-up position.
    auto* setUp = new QWidget;
    auto* setUpBox = new QVBoxLayout(setUp);
    setUpBox->setContentsMargins(0, 0, 0, 0);
    setUpBox->addWidget(label(tr("Not from the starting position"), "emptyTitle"));
    setUpBox->addWidget(
        label(tr("The explorer follows games from the usual starting position."), "emptyHint"));
    setUpBox->addStretch();
    m_pages->addWidget(setUp);

    connect(&m_databases, &DatabaseManager::changed, this, [this] {
        refreshSources();
        refresh();
    });
    connect(&m_repertoires, &RepertoireStore::changed, this, &ExplorerPanel::refresh);
    refreshSources();
    refresh();
}

void ExplorerPanel::setPosition(const core::Game* game, core::NodeId current,
                                core::Color viewSide) {
    m_game = game;
    m_current = current;
    m_viewSide = viewSide;
    refresh();
}

void ExplorerPanel::refreshSources() {
    const QString wanted = m_source->count() > 0
                               ? m_source->currentText()
                               : QSettings().value(QString::fromLatin1(kSourceKey)).toString();
    m_source->clear();
    for (const InstalledDatabase& database : m_databases.databases()) {
        m_source->addItem(QString::fromStdString(database.db->info().name));
    }
    const int index = m_source->findText(wanted);
    m_source->setCurrentIndex(index >= 0 ? index : 0);
}

void ExplorerPanel::refresh() {
    const int request = ++m_request;
    std::shared_ptr<const gamedb::Database> db;
    for (const InstalledDatabase& database : m_databases.databases()) {
        if (QString::fromStdString(database.db->info().name) == m_source->currentText()) {
            db = database.db;
        }
    }
    if (!db) {
        m_pages->setCurrentIndex(0);
        return;
    }
    if (m_game != nullptr && m_game->startPosition().hash() != core::Position().hash()) {
        m_pages->setCurrentIndex(2);
        return;
    }
    m_pages->setCurrentIndex(1);
    std::vector<core::Move> line;
    if (m_game != nullptr) {
        for (const core::NodeId node : m_game->path(m_current)) {
            if (node != core::Game::root()) {
                line.push_back(m_game->node(node).move);
            }
        }
    }
    // Your repertoire's moves here, for the side the board is viewed from.
    std::vector<core::Move> repertoire;
    if (const BuiltRepertoire* built = m_repertoires.get(m_viewSide)) {
        const core::Position position =
            m_game != nullptr ? m_game->positionAt(m_current) : core::Position();
        if (const repertoire::Entry* entry = built->tree.find(position.hash())) {
            for (const auto& continuation : entry->moves) {
                repertoire.push_back(continuation.move);
            }
        }
    }
    // Owned by the panel and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher =
        new QFutureWatcher<Answer>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<Answer>::finished, this, [this, watcher, request, repertoire] {
        watcher->deleteLater();
        if (request != m_request) {
            return; // the board has moved on
        }
        const Answer answer = watcher->result();
        const auto& explored = answer.explored;
        QString summary =
            explored.games == 1 ? tr("1 game") : tr("%1 games").arg(number(explored.games));
        if (const QString average = ratings(explored.whiteElo, explored.blackElo);
            !average.isEmpty()) {
            summary += QStringLiteral("  ·  ") + tr("rated %1").arg(average);
        }
        m_summary->setText(summary);
        m_total->setResults(explored.white, explored.draws, explored.black);
        m_moves->clear();
        const auto& c = m_style.colors();
        for (const gamedb::MoveStats& stats : explored.moves) {
            auto* item = new QTreeWidgetItem(m_moves);
            item->setData(0, kMoveRole, QString::fromStdString(stats.move.uci()));
            item->setText(0, QString::fromStdString(stats.san));
            item->setText(1, number(stats.games));
            item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            // Its share of the games here.
            const double share = explored.games > 0 ? 100.0 * static_cast<double>(stats.games) /
                                                          static_cast<double>(explored.games)
                                                    : 0.0;
            item->setText(2, share >= 1.0 ? QStringLiteral("%1%").arg(qRound(share))
                                          : QStringLiteral("<1%"));
            item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
            item->setForeground(2, c.muted);
            item->setText(4, ratings(stats.whiteElo, stats.blackElo));
            item->setForeground(4, c.muted);
            QFont san = item->font(0);
            san.setWeight(QFont::DemiBold);
            item->setFont(0, san);
            if (std::find(repertoire.begin(), repertoire.end(), stats.move) != repertoire.end()) {
                item->setForeground(0, c.accent);
                item->setToolTip(0, m_viewSide == core::Color::White
                                        ? tr("In your White repertoire")
                                        : tr("In your Black repertoire"));
            }
            // The bar in a box of its own, so it sits in the middle of the row.
            auto* cell = new QWidget;
            auto* cellBox = new QVBoxLayout(cell);
            cellBox->setContentsMargins(4, 0, 6, 0);
            auto* bar = new ResultBar(m_style, 14);
            bar->setResults(stats.white, stats.draws, stats.black);
            cellBox->addWidget(bar, 0, Qt::AlignVCenter);
            m_moves->setItemWidget(item, 3, cell);
        }
        m_games->clear();
        for (const gamedb::GameInfo& game : answer.games) {
            auto* item = new QTreeWidgetItem(m_games);
            item->setData(0, kGameRole, static_cast<qlonglong>(game.id));
            const auto player = [](const std::string& name, int elo) {
                const QString text = QString::fromStdString(name);
                return elo > 0 ? QStringLiteral("%1 (%2)").arg(text).arg(elo) : text;
            };
            item->setText(0, player(game.white, game.whiteElo));
            item->setText(1, player(game.black, game.blackElo));
            item->setText(2, resultText(game.result));
            item->setText(3, QString::fromStdString(game.date.substr(0, 4)));
            item->setForeground(3, c.muted);
            item->setToolTip(0, QString::fromStdString(game.event));
        }
        emit updated();
    });
    watcher->setFuture(QtConcurrent::run(
        [db, line = std::move(line)] { return Answer{db->explore(line), db->gamesAt(line, 20)}; }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

QStringList ExplorerPanel::moveTexts() const {
    QStringList texts;
    for (int i = 0; i < m_moves->topLevelItemCount(); ++i) {
        const auto* item = m_moves->topLevelItem(i);
        texts << QStringLiteral("%1 %2 %3").arg(item->text(0), item->text(1), item->text(4));
    }
    return texts;
}

} // namespace kiebitz::app
