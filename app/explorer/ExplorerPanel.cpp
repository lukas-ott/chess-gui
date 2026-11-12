#include "ExplorerPanel.hpp"

#include "LichessExplorer.hpp"
#include "databases/DatabaseManager.hpp"
#include "profiles/LichessLogin.hpp"
#include "profiles/ProfileStore.hpp"
#include "repertoire/RepertoireStore.hpp"
#include "style/AppStyle.hpp"
#include "widgets/ResultBar.hpp"
#include "widgets/SortCycle.hpp"
#include "widgets/SortableItem.hpp"

#include <QComboBox>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <array>
#include <tuple>
#include <utility>

namespace kiebitz::app {

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
    if (white == black) {
        return one(white); // one average for both (Lichess's explorer)
    }
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

ExplorerPanel::ExplorerPanel(DatabaseManager& databases, ProfileStore& profiles,
                             RepertoireStore& repertoires, LichessLogin& lichess,
                             const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_databases(databases), m_profiles(profiles), m_repertoires(repertoires),
      m_lichess(lichess), m_online(new LichessExplorer(lichess, this)), m_style(style) {
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
        m_sourceChosen = true;
        QSettings().setValue(QString::fromLatin1(kSourceKey), m_source->currentData());
        refreshParts();
        filterChanged(); // "Filter •" may change with the parts
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
                                            std::tuple{1, tr("Top Games"), "explorerShowGames"},
                                            std::tuple{2, tr("Filter"), "explorerShowFilter"}}) {
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
        if (index == 2) {
            m_filterTab = button;
        }
    }
    auto* top = new QHBoxLayout;
    top->setSpacing(8);
    top->addWidget(m_source);
    top->addWidget(views);
    top->addWidget(m_summary, 1);
    box->addLayout(top);
    // Lichess's explorer, signed out: what to do.
    m_signIn = new QFrame;
    m_signIn->setObjectName(QStringLiteral("explorerSignIn"));
    m_signIn->setProperty("statTile", true);
    auto* signInBox = new QVBoxLayout(m_signIn);
    signInBox->setContentsMargins(14, 12, 14, 12);
    signInBox->setSpacing(8);
    signInBox->addWidget(label(tr("Sign in to Lichess to use its explorer"), "checkHeadline"));
    signInBox->addWidget(label(tr("Lichess asks for a login for its opening explorer. Kiebitz "
                                  "only reads with it; your password stays with Lichess."),
                               "emptyHint"));
    auto* signIn = new QPushButton(tr("Sign In with Lichess"));
    signIn->setObjectName(QStringLiteral("explorerSignInButton"));
    signIn->setProperty("role", "primary");
    connect(signIn, &QPushButton::clicked, &m_lichess, [this] { m_lichess.signIn({}); });
    auto* signInRow = new QHBoxLayout;
    signInRow->addWidget(signIn);
    signInRow->addStretch();
    signInBox->addLayout(signInRow);
    m_signIn->hide();
    box->addWidget(m_signIn);
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
    // Sortable by any column (a click on its title); most played first to begin with.
    m_moves->setSortingEnabled(true);
    m_moves->sortByColumn(1, Qt::DescendingOrder);
    sortOnClicksThenReset(m_moves->header(), 1, Qt::DescendingOrder);
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
    connect(m_games, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item) { // opens it
                const auto id = item->data(0, kGameRole).toLongLong();
                if (onlineSource()) {
                    if (id >= 0 && std::cmp_less(id, m_onlineIds.size())) {
                        const auto kind = m_source->currentData().toString() == u"lichess:masters"
                                              ? LichessExplorer::Kind::Masters
                                              : LichessExplorer::Kind::Lichess;
                        m_online->fetchGame(kind, m_onlineIds[static_cast<std::size_t>(id)],
                                            [this](std::optional<core::Game> game) {
                                                if (game) {
                                                    emit openGameRequested(*game);
                                                }
                                            });
                    }
                    return;
                }
                if (const auto db = sourceDatabase()) {
                    if (const auto game = db->game(id)) {
                        emit openGameRequested(*game);
                    }
                }
            });
    lists->addWidget(m_games);
    lists->addWidget(buildFilter());
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
    connect(&m_profiles, &ProfileStore::changed, this, [this] {
        refreshSources();
        refresh();
    });
    connect(&m_repertoires, &RepertoireStore::changed, this, &ExplorerPanel::refresh);
    connect(&m_lichess, &LichessLogin::changed, this, &ExplorerPanel::refresh);
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
    // Each source by a key that stays the same when names change:
    //   "db:<folder>", "profile:<id>" (all its games) or "profile:<id>:<part>".
    // The source picked, now or last time; else the first one (a database or profile before
    // Lichess, so the first database added is shown).
    const QString wanted = m_sourceChosen
                               ? m_source->currentData().toString()
                               : QSettings().value(QString::fromLatin1(kSourceKey)).toString();
    const QSignalBlocker block(m_source);
    m_source->clear();
    for (const InstalledDatabase& database : m_databases.databases()) {
        m_source->addItem(QString::fromStdString(database.db->info().name),
                          QStringLiteral("db:") + database.folder);
    }
    for (const Profile& profile : m_profiles.profiles()) {
        if (!profile.all) {
            continue; // no games yet
        }
        // Once; its accounts and over-the-board games are chosen in the Filter tab.
        m_source->addItem(profile.name, QStringLiteral("profile:") + profile.id);
    }
    // Lichess's explorer: its own games and the masters' (it asks for a login).
    m_source->addItem(tr("Lichess"), QStringLiteral("lichess:lichess"));
    m_source->addItem(tr("Masters (Lichess)"), QStringLiteral("lichess:masters"));
    int index = m_source->findData(wanted);
    if (index < 0 && wanted.startsWith(u"profile:")) { // "profile:id:part" from before
        index = m_source->findData(wanted.section(u':', 0, 1));
    }
    m_sourceChosen = index >= 0;
    m_source->setCurrentIndex(index >= 0 ? index : 0);
    refreshParts();
}

void ExplorerPanel::refreshParts() {
    if (m_partBox == nullptr) {
        return;
    }
    const QString key = m_source->currentData().toString();
    const Profile* profile =
        key.startsWith(u"profile:") ? m_profiles.profile(key.section(u':', 1, 1)) : nullptr;
    m_partRow->setVisible(profile != nullptr);
    m_partTitle->setVisible(profile != nullptr);
    if (profile == nullptr) {
        m_part.clear();
        return;
    }
    // All, each account with games, the over-the-board games.
    std::vector<std::pair<QString, QString>> choices{{QString(), tr("All games")}};
    for (const Account& account : profile->accounts) {
        if (profile->parts.contains(account.id)) {
            choices.emplace_back(account.id,
                                 QStringLiteral("%1 · %2").arg(account.site == AccountSite::Lichess
                                                                   ? QStringLiteral("Lichess")
                                                                   : QStringLiteral("chess.com"),
                                                               account.username));
        }
    }
    if (profile->parts.contains(QStringLiteral("otb"))) {
        choices.emplace_back(QStringLiteral("otb"), tr("Over the board"));
    }
    if (std::none_of(choices.begin(), choices.end(),
                     [this](const auto& choice) { return choice.first == m_part; })) {
        m_part.clear(); // gone (another profile, an account removed)
    }
    const QSignalBlocker block(m_partBox);
    m_partBox->clear();
    for (const auto& [part, text] : choices) {
        m_partBox->addItem(text, part);
    }
    m_partBox->setCurrentIndex(std::max(0, m_partBox->findData(m_part)));
}

std::shared_ptr<const gamedb::Database> ExplorerPanel::sourceDatabase() const {
    const QString key = m_source->currentData().toString();
    if (key.startsWith(u"db:")) {
        for (const InstalledDatabase& database : m_databases.databases()) {
            if (QStringLiteral("db:") + database.folder == key) {
                return database.db;
            }
        }
        return nullptr;
    }
    if (!key.startsWith(u"profile:")) {
        return nullptr;
    }
    const Profile* profile = m_profiles.profile(key.section(u':', 1, 1));
    if (profile == nullptr) {
        return nullptr;
    }
    if (m_part.isEmpty()) {
        return profile->all;
    }
    const auto found = profile->parts.find(m_part); // chosen under "Games from"
    return found != profile->parts.end() ? found->second : nullptr;
}

void ExplorerPanel::refresh() {
    const int request = ++m_request;
    const std::shared_ptr<const gamedb::Database> db = sourceDatabase();
    if (!db && !onlineSource()) {
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
    // Lichess's explorer: over the network, signed in.
    const QString key = m_source->currentData().toString();
    const bool online = onlineSource();
    m_signIn->setVisible(online && !m_lichess.signedIn());
    for (QWidget* row : m_nameRows) {
        row->setVisible(!online);
    }
    if (online) {
        m_onlineIds.clear();
        if (m_sideRow != nullptr) {
            m_sideRow->hide();
            m_sideTitle->hide();
        }
        if (!m_lichess.signedIn()) {
            showAnswer({}, {}, {});
            m_summary->setText(QString());
            return;
        }
        m_summary->setText(tr("Asking Lichess…"));
        const auto kind = key == u"lichess:masters" ? LichessExplorer::Kind::Masters
                                                    : LichessExplorer::Kind::Lichess;
        m_online->ask(kind, line, m_filter,
                      [this, request, repertoire](const LichessExplorer::Answer& answer) {
                          if (request != m_request) {
                              return;
                          }
                          m_onlineIds = answer.ids;
                          showAnswer(answer.explored, answer.games, repertoire);
                          if (!answer.error.isEmpty()) {
                              m_summary->setText(answer.error);
                          }
                      });
        return;
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
        showAnswer(answer.explored, answer.games, repertoire);
        emit updated();
    });
    // The filter; for a profile with a colour chosen, the profile's names in that part.
    gamedb::ExploreFilter filter = m_filter;
    filter.players.clear();
    const bool profile = key.startsWith(u"profile:");
    if (m_sideRow != nullptr) {
        m_sideRow->setVisible(profile);
        m_sideTitle->setVisible(profile);
    }
    if (profile && filter.side != gamedb::ExploreFilter::Side::Any) {
        if (const Profile* p = m_profiles.profile(key.section(u':', 1, 1))) {
            for (const QString& name : ProfileStore::namesOf(*p, m_part)) {
                filter.players.push_back(name.toStdString());
            }
        }
    } else {
        filter.side = gamedb::ExploreFilter::Side::Any;
    }
    watcher->setFuture(QtConcurrent::run([db, line = std::move(line), filter] {
        return Answer{db->explore(line, filter), db->gamesAt(line, 20, filter)};
    }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

GapSource ExplorerPanel::gapSource() {
    GapSource source;
    source.name = m_source->currentText();
    if (onlineSource()) {
        if (!m_lichess.signedIn()) {
            source.unavailable = tr("Sign in to Lichess to check against its games, or choose a "
                                    "database in the Explorer tab.");
            return source;
        }
        const auto kind = m_source->currentData().toString() == u"lichess:masters"
                              ? LichessExplorer::Kind::Masters
                              : LichessExplorer::Kind::Lichess;
        source.explore = m_online->blocking(kind, m_filter);
        return source;
    }
    const std::shared_ptr<const gamedb::Database> db = sourceDatabase();
    if (!db) {
        source.unavailable = tr("Add a database or sign in to Lichess, then choose it in the "
                                "Explorer tab.");
        return source;
    }
    gamedb::ExploreFilter filter = m_filter;
    filter.players.clear();
    filter.side = gamedb::ExploreFilter::Side::Any;
    source.explore = [db, filter](std::span<const core::Move> line) {
        std::vector<repertoire::MoveCount> result;
        for (const auto& stats : db->explore(line, filter).moves) {
            result.push_back({stats.move, stats.games});
        }
        return result;
    };
    return source;
}

bool ExplorerPanel::onlineSource() const {
    return m_source->currentData().toString().startsWith(u"lichess:");
}

void ExplorerPanel::showAnswer(const gamedb::Explored& explored,
                               const std::vector<gamedb::GameInfo>& games,
                               const std::vector<core::Move>& repertoire) {
    QString summary =
        explored.games == 1 ? tr("1 game") : tr("%1 games").arg(number(explored.games));
    if (const QString average = ratings(explored.whiteElo, explored.blackElo); !average.isEmpty()) {
        summary += QStringLiteral("  ·  ") + tr("rated %1").arg(average);
    }
    m_summary->setText(summary);
    m_total->setResults(explored.white, explored.draws, explored.black);
    m_moves->clear();
    const auto& c = m_style.colors();
    m_moves->setSortingEnabled(false); // filled first, then sorted once
    for (const gamedb::MoveStats& stats : explored.moves) {
        auto* item = new SortableItem(m_moves);
        // What each column sorts by: games, share, White's score, rating.
        const auto played = static_cast<double>(stats.games);
        item->setSortValue(1, played);
        item->setSortValue(2, played);
        item->setSortValue(3, stats.games > 0 ? (static_cast<double>(stats.white) +
                                                 (0.5 * static_cast<double>(stats.draws))) /
                                                    played
                                              : 0.0);
        item->setSortValue(4, (stats.whiteElo + stats.blackElo) / 2.0);
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
            item->setToolTip(0, m_viewSide == core::Color::White ? tr("In your White repertoire")
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
    m_moves->setSortingEnabled(true); // sorts by the column chosen
    m_games->clear();
    for (const gamedb::GameInfo& game : games) {
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
}

QWidget* ExplorerPanel::buildFilter() {
    // Rating, years, time control and (for a profile) the colour it played; remembered.
    const QSettings settings;
    m_filter.minRating = settings.value(QStringLiteral("explorer/minRating"), 0).toInt();
    m_filter.maxRating = settings.value(QStringLiteral("explorer/maxRating"), 0).toInt();
    m_filter.fromYear = settings.value(QStringLiteral("explorer/fromYear"), 0).toInt();
    m_filter.toYear = settings.value(QStringLiteral("explorer/toYear"), 0).toInt();
    m_filter.speeds =
        static_cast<std::uint8_t>(settings.value(QStringLiteral("explorer/speeds"), 0).toUInt());
    m_filter.white = settings.value(QStringLiteral("explorer/white")).toString().toStdString();
    m_filter.black = settings.value(QStringLiteral("explorer/black")).toString().toStdString();

    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("explorerFilter"));
    auto* grid = new QGridLayout(page);
    grid->setContentsMargins(2, 8, 2, 2);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    grid->setColumnStretch(2, 1);
    int row = 0;
    const auto caption = [&](const QString& text) {
        auto* title = new QLabel(text);
        title->setProperty("role", "infoLabel");
        grid->addWidget(title, row, 0, Qt::AlignVCenter);
        return title;
    };
    // A from–to pair of numbers; empty means "any".
    const auto range = [&](const char* name, int low, int high, int* from, int* to) {
        auto* box = new QHBoxLayout;
        box->setSpacing(6);
        std::array<QLineEdit*, 2> fields{};
        for (std::size_t k = 0; k < 2; ++k) {
            int* value = k == 0 ? from : to;
            auto* field = new QLineEdit(*value > 0 ? QString::number(*value) : QString());
            field->setObjectName(QStringLiteral("%1%2").arg(
                QLatin1String(name), k == 0 ? QStringLiteral("From") : QStringLiteral("To")));
            field->setPlaceholderText(tr("any"));
            field->setValidator(new QIntValidator(0, high, field));
            field->setFixedWidth(76);
            field->setAlignment(Qt::AlignCenter);
            connect(field, &QLineEdit::textChanged, this, [this, value, low](const QString& text) {
                const int number = text.toInt();
                *value = number >= low ? number : 0; // until it is a whole year or rating
                filterChanged();
            });
            fields[k] = field;
        }
        box->addWidget(fields[0]);
        box->addWidget(new QLabel(QStringLiteral("–")));
        box->addWidget(fields[1]);
        box->addStretch();
        grid->addLayout(box, row, 1, 1, 2);
        ++row;
        return fields;
    };
    // Players by name (part of it, any case), for each colour.
    const auto player = [&](const QString& title, const char* name, std::string* text) {
        m_nameRows.push_back(caption(title));
        auto* field = new QLineEdit(QString::fromStdString(*text));
        field->setObjectName(QString::fromLatin1(name));
        field->setPlaceholderText(tr("Any player"));
        field->setClearButtonEnabled(true);
        field->setMaximumWidth(240);
        auto* delay = new QTimer(field);
        delay->setSingleShot(true);
        delay->setInterval(400); // after typing stops
        connect(field, &QLineEdit::textChanged, delay, qOverload<>(&QTimer::start));
        connect(delay, &QTimer::timeout, this, [this, field, text] {
            *text = field->text().trimmed().toStdString();
            filterChanged();
        });
        grid->addWidget(field, row++, 1, 1, 2);
        m_nameRows.push_back(field);
        return field;
    };
    QLineEdit* whiteName = player(tr("White"), "explorerWhite", &m_filter.white);
    QLineEdit* blackName = player(tr("Black"), "explorerBlack", &m_filter.black);
    caption(tr("Rating"));
    const auto ratings =
        range("explorerRating", 600, 3500, &m_filter.minRating, &m_filter.maxRating);
    caption(tr("Years"));
    const auto years = range("explorerYear", 1800, 2100, &m_filter.fromYear, &m_filter.toYear);

    // Time controls: none chosen means all of them.
    caption(tr("Time control"));
    auto* speeds = new QHBoxLayout;
    speeds->setSpacing(4);
    std::vector<QPushButton*> speedButtons;
    for (const auto& [speed, text] : {std::pair{gamedb::Speed::Bullet, tr("Bullet")},
                                      std::pair{gamedb::Speed::Blitz, tr("Blitz")},
                                      std::pair{gamedb::Speed::Rapid, tr("Rapid")},
                                      std::pair{gamedb::Speed::Classical, tr("Classical")},
                                      std::pair{gamedb::Speed::Correspondence, tr("Corr.")}}) {
        auto* chip = new QPushButton(text);
        chip->setProperty("role", "chip");
        chip->setCheckable(true);
        chip->setFocusPolicy(Qt::NoFocus);
        const auto bit = static_cast<std::uint8_t>(1U << static_cast<unsigned>(speed));
        chip->setChecked((m_filter.speeds & bit) != 0);
        connect(chip, &QPushButton::toggled, this, [this, bit](bool on) {
            const unsigned speeds = m_filter.speeds;
            m_filter.speeds =
                static_cast<std::uint8_t>(on ? (speeds | bit) : (speeds & ~unsigned{bit}));
            filterChanged();
        });
        speeds->addWidget(chip);
        speedButtons.push_back(chip);
    }
    speeds->addStretch();
    grid->addLayout(speeds, row++, 1, 1, 2);

    // For a profile: the games of one account, or over the board.
    m_partRow = new QWidget;
    auto* partBox = new QHBoxLayout(m_partRow);
    partBox->setContentsMargins(0, 0, 0, 0);
    partBox->setSpacing(0);
    m_partTitle = new QLabel(tr("Games from"));
    m_partTitle->setProperty("role", "infoLabel");
    grid->addWidget(m_partTitle, row, 0);
    m_partBox = new QComboBox;
    m_partBox->setObjectName(QStringLiteral("explorerPart"));
    m_partBox->setFocusPolicy(Qt::NoFocus);
    m_partBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(m_partBox, &QComboBox::activated, this, [this] {
        m_part = m_partBox->currentData().toString();
        filterChanged();
    });
    partBox->addWidget(m_partBox);
    partBox->addStretch();
    grid->addWidget(m_partRow, row++, 1, 1, 2);

    // For a profile: its games with one colour.
    m_sideRow = new QWidget;
    auto* sideBox = new QHBoxLayout(m_sideRow);
    sideBox->setContentsMargins(0, 0, 0, 0);
    sideBox->setSpacing(0);
    m_sideTitle = new QLabel(tr("Played as"));
    m_sideTitle->setProperty("role", "infoLabel");
    grid->addWidget(m_sideTitle, row, 0);
    auto* sides = new QWidget;
    sides->setProperty("role", "segmented");
    auto* sidesRow = new QHBoxLayout(sides);
    sidesRow->setContentsMargins(2, 2, 2, 2);
    sidesRow->setSpacing(2);
    std::vector<QPushButton*> sideButtons;
    for (const auto& [side, text] : {std::pair{gamedb::ExploreFilter::Side::Any, tr("Both")},
                                     std::pair{gamedb::ExploreFilter::Side::White, tr("White")},
                                     std::pair{gamedb::ExploreFilter::Side::Black, tr("Black")}}) {
        auto* segment = new QPushButton(text);
        segment->setProperty("role", "segment");
        segment->setCheckable(true);
        segment->setAutoExclusive(true);
        segment->setChecked(side == gamedb::ExploreFilter::Side::Any);
        segment->setFocusPolicy(Qt::NoFocus);
        segment->setObjectName(QStringLiteral("explorerSide%1").arg(static_cast<int>(side)));
        connect(segment, &QPushButton::clicked, this, [this, side] {
            m_filter.side = side;
            filterChanged();
        });
        sidesRow->addWidget(segment);
        sideButtons.push_back(segment);
    }
    sideBox->addWidget(sides);
    sideBox->addStretch();
    grid->addWidget(m_sideRow, row++, 1, 1, 2);

    auto* reset = new QPushButton(tr("Reset"));
    reset->setObjectName(QStringLiteral("explorerFilterReset"));
    reset->setProperty("role", "secondary");
    m_resetFilter = [this, ratings, years, speedButtons, sideButtons, whiteName, blackName] {
        m_part.clear();
        refreshParts();
        whiteName->clear();
        blackName->clear();
        m_filter.white.clear();
        m_filter.black.clear();
        for (QLineEdit* field : {ratings[0], ratings[1], years[0], years[1]}) {
            field->clear();
        }
        for (QPushButton* chip : speedButtons) {
            chip->setChecked(false);
        }
        sideButtons.front()->click();
    };
    connect(reset, &QPushButton::clicked, this, [this] { m_resetFilter(); });
    auto* resetRow = new QHBoxLayout;
    resetRow->addWidget(reset);
    resetRow->addStretch();
    grid->addLayout(resetRow, row++, 0, 1, 3);
    grid->setRowStretch(row, 1);
    filterChanged();
    // Rows keep their height; a short tab scrolls instead of squeezing them.
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("explorerFilterScroll"));
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

void ExplorerPanel::filterChanged() {
    QSettings settings;
    settings.setValue(QStringLiteral("explorer/minRating"), m_filter.minRating);
    settings.setValue(QStringLiteral("explorer/maxRating"), m_filter.maxRating);
    settings.setValue(QStringLiteral("explorer/fromYear"), m_filter.fromYear);
    settings.setValue(QStringLiteral("explorer/toYear"), m_filter.toYear);
    settings.setValue(QStringLiteral("explorer/speeds"), m_filter.speeds);
    settings.setValue(QStringLiteral("explorer/white"), QString::fromStdString(m_filter.white));
    settings.setValue(QStringLiteral("explorer/black"), QString::fromStdString(m_filter.black));
    const bool active =
        m_filter.active() || m_filter.side != gamedb::ExploreFilter::Side::Any || !m_part.isEmpty();
    if (m_filterTab != nullptr) {
        m_filterTab->setText(active ? tr("Filter •") : tr("Filter"));
    }
    refresh();
}

QStringList ExplorerPanel::moveTexts() const {
    QStringList texts;
    for (int i = 0; i < m_moves->topLevelItemCount(); ++i) {
        const auto* item = m_moves->topLevelItem(i);
        texts << QStringLiteral("%1 %2 %3").arg(item->text(0), item->text(1), item->text(4));
    }
    return texts;
}

} // namespace kiebitz::app
