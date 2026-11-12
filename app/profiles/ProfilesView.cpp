#include "ProfilesView.hpp"

#include "AddAccountDialog.hpp"
#include "FideCard.hpp"
#include "LichessLogin.hpp"
#include "ProfileStore.hpp"
#include "databases/GamesModel.hpp"
#include "repertoire/RepertoireStore.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"
#include "style/Layout.hpp"
#include "style/PageHeader.hpp"
#include "widgets/ActivityChart.hpp"
#include "widgets/DateRange.hpp"
#include "widgets/ElidedLabel.hpp"
#include "widgets/Message.hpp"
#include "widgets/NameDialog.hpp"
#include "widgets/ResultBar.hpp"
#include "widgets/SortCycle.hpp"
#include "widgets/SortableItem.hpp"

#include <QComboBox>
#include <QCryptographicHash>
#include <QFile>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableView>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <array>
#include <map>
#include <optional>
#include <tuple>
#include <utility>

namespace kiebitz::app {
namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr auto kOtb = "otb";
constexpr int kAccountsWidth = 300;
constexpr int kGroupRole = Qt::UserRole + 1; ///< a theory row: its group in the report
constexpr int kGameRole = Qt::UserRole + 2;  ///< a theory game row: its game

QLabel* label(const QString& text, const char* role) {
    auto* result = new QLabel(text);
    result->setProperty("role", role);
    return result;
}

QFrame* card() {
    auto* frame = new QFrame;
    frame->setProperty("card", true);
    return frame;
}

QString number(quint64 value) {
    return QLocale().toString(static_cast<qulonglong>(value));
}

QString gamesText(quint64 games) {
    return games == 1 ? ProfilesView::tr("1 game")
                      : ProfilesView::tr("%1 games").arg(number(games));
}

/// "just now", "5 min ago", "3 h ago", "2 days ago".
QString ago(const QDateTime& when) {
    if (!when.isValid()) {
        return ProfilesView::tr("not synced yet");
    }
    const qint64 seconds = when.secsTo(QDateTime::currentDateTime());
    if (seconds < 60) {
        return ProfilesView::tr("synced just now");
    }
    if (seconds < 3600) {
        return ProfilesView::tr("synced %1 min ago").arg(seconds / 60);
    }
    if (seconds < 86400) {
        return ProfilesView::tr("synced %1 h ago").arg(seconds / 3600);
    }
    return ProfilesView::tr("synced %1 days ago").arg(seconds / 86400);
}

/// "54%": the score of won, drawn, lost; "–" without games.
QString scoreText(std::uint64_t won, std::uint64_t drawn, std::uint64_t lost) {
    const std::uint64_t games = won + drawn + lost;
    if (games == 0) {
        return QStringLiteral("–");
    }
    const double points = static_cast<double>(won) + (0.5 * static_cast<double>(drawn));
    return QStringLiteral("%1%").arg(qRound(100.0 * points / static_cast<double>(games)));
}

/// A theory check's result as JSON, and back (with the games it counts).
QJsonObject theoryToJson(const QString& fingerprint, const repertoire::TheoryReport& report,
                         const std::vector<gamedb::GameInfo>& games) {
    QJsonArray groups;
    for (const repertoire::DepartureGroup& group : report.groups) {
        QJsonArray line;
        for (const core::Move& move : group.line) {
            line.append(QString::fromStdString(move.uci()));
        }
        QJsonArray expected;
        for (const std::string& san : group.expected) {
            expected.append(QString::fromStdString(san));
        }
        QJsonArray indexes;
        for (const std::size_t index : group.games) {
            indexes.append(static_cast<qint64>(index));
        }
        groups.append(QJsonObject{
            {QStringLiteral("kind"), static_cast<int>(group.kind)},
            {QStringLiteral("side"), group.side == core::Color::White ? 0 : 1},
            {QStringLiteral("line"), line},
            {QStringLiteral("played"), QString::fromStdString(group.played.uci())},
            {QStringLiteral("san"), QString::fromStdString(group.san)},
            {QStringLiteral("expected"), expected},
            {QStringLiteral("games"), indexes},
        });
    }
    QJsonArray infos;
    for (const gamedb::GameInfo& game : games) {
        infos.append(QJsonArray{static_cast<qint64>(game.id), QString::fromStdString(game.white),
                                QString::fromStdString(game.black),
                                QString::fromStdString(game.date), static_cast<int>(game.result)});
    }
    return {{QStringLiteral("fingerprint"), fingerprint},
            {QStringLiteral("checked"), static_cast<qint64>(report.checked)},
            {QStringLiteral("stayed"), static_cast<qint64>(report.stayed)},
            {QStringLiteral("average"), report.averageBookMoves},
            {QStringLiteral("groups"), groups},
            {QStringLiteral("games"), infos}};
}

std::optional<std::tuple<QString, repertoire::TheoryReport, std::vector<gamedb::GameInfo>>>
theoryFromJson(const QJsonObject& object) {
    repertoire::TheoryReport report;
    report.checked = static_cast<std::size_t>(object[QStringLiteral("checked")].toInteger());
    report.stayed = static_cast<std::size_t>(object[QStringLiteral("stayed")].toInteger());
    report.averageBookMoves = object[QStringLiteral("average")].toDouble();
    std::vector<gamedb::GameInfo> games;
    for (const auto& value : object[QStringLiteral("games")].toArray()) {
        const QJsonArray info = value.toArray();
        gamedb::GameInfo game;
        game.id = static_cast<gamedb::GameId>(info.at(0).toInteger());
        game.white = info.at(1).toString().toStdString();
        game.black = info.at(2).toString().toStdString();
        game.date = info.at(3).toString().toStdString();
        game.result = static_cast<gamedb::GameResult>(info.at(4).toInt());
        games.push_back(std::move(game));
    }
    for (const auto& value : object[QStringLiteral("groups")].toArray()) {
        const QJsonObject entry = value.toObject();
        repertoire::DepartureGroup group;
        group.kind = static_cast<repertoire::Departure>(entry[QStringLiteral("kind")].toInt());
        group.side =
            entry[QStringLiteral("side")].toInt() == 0 ? core::Color::White : core::Color::Black;
        for (const auto& move : entry[QStringLiteral("line")].toArray()) {
            const auto parsed = core::Move::fromUci(move.toString().toStdString());
            if (!parsed) {
                return std::nullopt; // not ours, or damaged: worked out again
            }
            group.line.push_back(*parsed);
        }
        const auto played =
            core::Move::fromUci(entry[QStringLiteral("played")].toString().toStdString());
        if (!played) {
            return std::nullopt;
        }
        group.played = *played;
        group.san = entry[QStringLiteral("san")].toString().toStdString();
        for (const auto& san : entry[QStringLiteral("expected")].toArray()) {
            group.expected.push_back(san.toString().toStdString());
        }
        for (const auto& index : entry[QStringLiteral("games")].toArray()) {
            const auto at = static_cast<std::size_t>(index.toInteger());
            if (at >= games.size()) {
                return std::nullopt;
            }
            group.games.push_back(at);
        }
        report.groups.push_back(std::move(group));
    }
    return std::tuple{object[QStringLiteral("fingerprint")].toString(), std::move(report),
                      std::move(games)};
}

/// A chip: a small toggle, one of a group where one is on.
QPushButton* chip(const QString& text, const QString& name) {
    auto* button = new QPushButton(text);
    button->setObjectName(name);
    button->setProperty("role", "chip");
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

QString siteText(AccountSite site) {
    return site == AccountSite::Lichess ? QStringLiteral("Lichess") : QStringLiteral("chess.com");
}

} // namespace

ProfilesView::ProfilesView(ProfileStore& store, LichessLogin& lichess, RepertoireStore& repertoires,
                           const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_store(store), m_repertoires(repertoires), m_lichess(lichess),
      m_style(style), m_model(new GamesModel(this)) {
    setObjectName(QStringLiteral("profilesView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(layout::kPage);
    layout->setSpacing(layout::kPageSpacing);
    m_header = new PageHeader(tr("Profiles"));
    auto* add = new QPushButton(tr("New Profile"));
    add->setObjectName(QStringLiteral("newProfile"));
    add->setProperty("role", "secondary");
    connect(add, &QPushButton::clicked, this, &ProfilesView::newProfile);
    m_header->addAction(add);
    layout->addWidget(m_header);

    // The profiles: you first.
    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("profileList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSpacing(2);
    connect(m_list, &QListWidget::currentRowChanged, this, [this] {
        if (const auto* item = m_list->currentItem()) {
            m_current = item->data(kIdRole).toString();
        }
        showProfile();
    });
    auto* listCard = card();
    auto* listBox = new QVBoxLayout(listCard);
    listBox->setContentsMargins(layout::kCard);
    listBox->addWidget(m_list);

    // The profile shown: its name with the FIDE "i", what it has, and what to do with it.
    m_name = label(QString(), "pageTitle");
    m_name->setObjectName(QStringLiteral("profileName"));
    m_summary = label(QString(), "emptyHint");
    m_summary->setObjectName(QStringLiteral("profileSummary"));
    m_status = label(QString(), "emptyHint");
    m_status->setObjectName(QStringLiteral("profileStatus"));
    const auto& c = m_style.colors();
    m_sync = new QPushButton(tr("Sync"));
    m_sync->setObjectName(QStringLiteral("profileSync"));
    m_sync->setProperty("role", "primary");
    m_sync->setIcon(makeIcon(IconKind::Refresh, c.accentText, c.dimmed, 14));
    m_sync->setToolTip(tr("Download the new games of this profile's accounts"));
    connect(m_sync, &QPushButton::clicked, this, [this] { m_store.sync(m_current); });
    m_rename = new QPushButton(tr("Rename"));
    m_rename->setProperty("role", "secondary");
    connect(m_rename, &QPushButton::clicked, this, [this] {
        const Profile* profile = m_store.profile(m_current);
        if (profile == nullptr) {
            return;
        }
        NameDialog dialog(tr("Rename Profile"), tr("Rename"), tr("Their name"), profile->name,
                          this);
        if (dialog.exec() == QDialog::Accepted) {
            m_store.renameProfile(m_current, dialog.name());
        }
    });
    m_remove = new QPushButton(tr("Remove"));
    m_remove->setProperty("role", "secondary");
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const Profile* profile = m_store.profile(m_current);
        if (profile == nullptr || profile->main) {
            return;
        }
        if (confirm(this, tr("Remove Profile"),
                    tr("Remove “%1” and the games downloaded for it?").arg(profile->name),
                    tr("Remove"), MessageDialog::Action::Destructive)) {
            m_store.removeProfile(m_current);
        }
    });
    m_info = new QPushButton;
    m_info->setObjectName(QStringLiteral("profileFide"));
    m_info->setProperty("role", "nav");
    m_info->setToolTip(tr("FIDE rating list"));
    m_info->setFixedSize(26, 26);
    m_info->setIconSize(QSize(16, 16));
    connect(m_info, &QPushButton::clicked, this, &ProfilesView::showFideCard);
    auto* nameRow = new QHBoxLayout;
    nameRow->setSpacing(6);
    nameRow->addWidget(m_name);
    nameRow->addWidget(m_info, 0, Qt::AlignVCenter);
    nameRow->addStretch();
    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    titles->addLayout(nameRow);
    titles->addWidget(m_summary);
    auto* top = new QHBoxLayout;
    top->setSpacing(6);
    top->addLayout(titles, 1);
    top->addWidget(m_rename, 0, Qt::AlignTop);
    top->addWidget(m_remove, 0, Qt::AlignTop);
    top->addWidget(m_sync, 0, Qt::AlignTop);

    // Left: the accounts, one card each, then the over-the-board games and "Add Account".
    auto* accountsContent = new QWidget;
    accountsContent->setObjectName(QStringLiteral("profileAccountsContent"));
    m_accounts = new QVBoxLayout(accountsContent);
    m_accounts->setContentsMargins(0, 0, 6, 0);
    m_accounts->setSpacing(10);
    auto* accountsScroll = new QScrollArea;
    accountsScroll->setObjectName(QStringLiteral("profileAccounts"));
    accountsScroll->setWidget(accountsContent);
    accountsScroll->setWidgetResizable(true);
    accountsScroll->setFrameShape(QFrame::NoFrame);
    accountsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    accountsScroll->setFixedWidth(kAccountsWidth);

    // Right: the games looked at (all, one account, over the board), as an overview or a list.
    auto* views = new QWidget;
    views->setProperty("role", "segmented");
    auto* viewsRow = new QHBoxLayout(views);
    viewsRow->setContentsMargins(2, 2, 2, 2);
    viewsRow->setSpacing(2);
    m_pages = new QStackedWidget;
    for (const auto& [index, text, name] :
         {std::tuple{0, tr("Overview"), "profileShowOverview"},
          std::tuple{1, tr("Games"), "profileShowGames"},
          std::tuple{2, tr("Repertoire"), "profileShowRepertoire"}}) {
        auto* button = new QPushButton(text);
        button->setObjectName(QString::fromLatin1(name));
        button->setProperty("role", "segment");
        button->setCheckable(true);
        button->setChecked(index == 0);
        button->setAutoExclusive(true);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, m_pages, [this, index] {
            m_pages->setCurrentIndex(index);
            if (index == 2) {
                showTheory();
            }
        });
        viewsRow->addWidget(button);
    }
    m_parts = new QComboBox;
    m_parts->setObjectName(QStringLiteral("profilePart"));
    m_parts->setFocusPolicy(Qt::NoFocus);
    m_parts->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(m_parts, &QComboBox::activated, this, [this] {
        m_part = m_parts->currentData().toString();
        showGames();
    });
    auto* panelTop = new QHBoxLayout;
    panelTop->setSpacing(10);
    views->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); // never squeezed
    panelTop->addWidget(views);
    panelTop->addStretch();
    panelTop->addWidget(label(tr("Games from"), "emptyHint"));
    panelTop->addWidget(m_parts);

    // Overview: the results, by colour, and the games per year, at one time control or all.
    auto* overview = new QWidget;
    auto* overviewBox = new QVBoxLayout(overview);
    overviewBox->setContentsMargins(0, 0, 0, 0);
    overviewBox->setSpacing(12);
    auto* speedsRow = new QHBoxLayout;
    speedsRow->setSpacing(4);
    for (const auto& [bits, text, name] :
         {std::tuple{0U, tr("All"), "all"}, std::tuple{1U << 1U, tr("Bullet"), "bullet"},
          std::tuple{1U << 2U, tr("Blitz"), "blitz"}, std::tuple{1U << 3U, tr("Rapid"), "rapid"},
          std::tuple{1U << 4U, tr("Classical"), "classical"},
          std::tuple{1U << 5U, tr("Correspondence"), "correspondence"}}) {
        auto* button = chip(text, QStringLiteral("profileSpeed:") + QString::fromLatin1(name));
        button->setChecked(bits == 0);
        connect(button, &QPushButton::clicked, this, [this, speedBits = bits] {
            m_speeds = static_cast<std::uint8_t>(speedBits);
            showStats();
        });
        speedsRow->addWidget(button);
    }
    speedsRow->addStretch();
    overviewBox->addLayout(speedsRow);

    auto* results = new QFrame;
    results->setProperty("statTile", true);
    auto* resultsBox = new QVBoxLayout(results);
    resultsBox->setContentsMargins(14, 12, 14, 12);
    resultsBox->setSpacing(8);
    auto* resultsHead = new QHBoxLayout;
    resultsHead->addWidget(label(tr("RESULTS"), "cardTitle"));
    resultsHead->addStretch();
    m_totalText = label(QString(), "statValue");
    m_totalText->setObjectName(QStringLiteral("profileTotal"));
    resultsHead->addWidget(m_totalText);
    resultsBox->addLayout(resultsHead);
    m_totalBar = new ResultBar(m_style, 22, ResultBar::Kind::Record);
    resultsBox->addWidget(m_totalBar);
    m_legend = label(QString(), "emptyHint");
    m_legend->setObjectName(QStringLiteral("profileLegend"));
    m_legend->setTextFormat(Qt::RichText);
    resultsBox->addWidget(m_legend);
    overviewBox->addWidget(results);

    auto* colours = new QHBoxLayout;
    colours->setSpacing(12);
    const auto colourTile = [&](const QString& caption, QLabel*& text, ResultBar*& bar) {
        auto* tile = new QFrame;
        tile->setProperty("statTile", true);
        auto* tileBox = new QVBoxLayout(tile);
        tileBox->setContentsMargins(14, 10, 14, 12);
        tileBox->setSpacing(6);
        auto* head = new QHBoxLayout;
        head->addWidget(label(caption, "cardTitle"));
        head->addStretch();
        text = label(QString(), "emptyHint");
        head->addWidget(text);
        tileBox->addLayout(head);
        bar = new ResultBar(m_style, 14, ResultBar::Kind::Record);
        tileBox->addWidget(bar);
        colours->addWidget(tile, 1);
    };
    colourTile(tr("AS WHITE"), m_whiteText, m_whiteBar);
    colourTile(tr("AS BLACK"), m_blackText, m_blackBar);
    m_whiteText->setObjectName(QStringLiteral("profileAsWhite"));
    m_blackText->setObjectName(QStringLiteral("profileAsBlack"));
    overviewBox->addLayout(colours);

    auto* activity = new QFrame;
    activity->setProperty("statTile", true);
    auto* activityBox = new QVBoxLayout(activity);
    activityBox->setContentsMargins(14, 12, 14, 10);
    activityBox->setSpacing(8);
    activityBox->addWidget(label(tr("ACTIVITY"), "cardTitle"));
    m_activity = new ActivityChart(m_style);
    activityBox->addWidget(m_activity, 1);
    overviewBox->addWidget(activity, 1);
    m_pages->addWidget(overview);

    // Games: the list, searchable; a double click opens one.
    auto* gamesPage = new QWidget;
    auto* gamesBox = new QVBoxLayout(gamesPage);
    gamesBox->setContentsMargins(0, 0, 0, 0);
    gamesBox->setSpacing(8);
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("profileSearch"));
    m_search->setPlaceholderText(tr("Search players, events, ECO"));
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(320);
    auto* searchDelay = new QTimer(this);
    searchDelay->setSingleShot(true);
    searchDelay->setInterval(250);
    connect(m_search, &QLineEdit::textChanged, searchDelay, qOverload<>(&QTimer::start));
    connect(searchDelay, &QTimer::timeout, this, [this] { m_model->setText(m_search->text()); });
    m_dates = new DateRange(QStringLiteral("profileDate"), m_style);
    connect(m_dates, &DateRange::changed, this,
            [this] { m_model->setDates(m_dates->from(), m_dates->to()); });
    m_gamesStatus = label(QString(), "emptyHint");
    m_gamesStatus->setObjectName(QStringLiteral("profileGamesStatus"));
    connect(m_model, &GamesModel::statusChanged, this,
            [this] { m_gamesStatus->setText(m_model->status()); });
    m_games = new QTableView;
    m_games->setObjectName(QStringLiteral("profileGames"));
    m_games->setModel(m_model);
    GamesModel::setUpTable(*m_games);
    // Delete a game: from its menu, or with the Delete key.
    m_games->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_games, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        const QModelIndex index = m_games->indexAt(at);
        if (!index.isValid()) {
            return;
        }
        m_games->setCurrentIndex(index);
        QMenu menu;
        menu.addAction(tr("Open"), this, [this, index] { emit m_games->doubleClicked(index); });
        menu.addSeparator();
        menu.addAction(tr("Delete Game…"), this, &ProfilesView::deleteSelectedGame);
        menu.exec(m_games->viewport()->mapToGlobal(at));
    });
    auto* deleteKey = new QShortcut(QKeySequence::Delete, m_games);
    deleteKey->setContext(Qt::WidgetShortcut);
    connect(deleteKey, &QShortcut::activated, this, &ProfilesView::deleteSelectedGame);
    auto* backspace = new QShortcut(QKeySequence(Qt::Key_Backspace), m_games);
    backspace->setContext(Qt::WidgetShortcut);
    connect(backspace, &QShortcut::activated, this, &ProfilesView::deleteSelectedGame);
    connect(m_games, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        const gamedb::GameInfo* info = m_model->at(index.row());
        if (info != nullptr && m_db) {
            if (const auto game = m_db->game(info->id)) {
                // As its PGN has it: with its clocks and comments.
                m_store.originalGame(m_current, m_part, *game, [this](const core::Game& whole) {
                    emit openGameRequested(whole);
                });
            }
        }
    });
    auto* gamesTop = new QHBoxLayout;
    gamesTop->setSpacing(6);
    gamesTop->addWidget(m_search, 1);
    gamesTop->addWidget(m_dates);
    gamesTop->addSpacing(6);
    gamesTop->addWidget(m_gamesStatus);
    gamesBox->addLayout(gamesTop);
    gamesBox->addWidget(m_games, 1);
    m_pages->addWidget(gamesPage);

    // Repertoire: where these games left your repertoire, grouped by position and move.
    auto* theoryPage = new QWidget;
    auto* theoryBox = new QVBoxLayout(theoryPage);
    theoryBox->setContentsMargins(0, 0, 0, 0);
    theoryBox->setSpacing(8);
    auto* kinds = new QHBoxLayout;
    kinds->setSpacing(4);
    for (const auto& [kind, text, name] :
         {std::tuple{-1, tr("All"), "all"},
          std::tuple{static_cast<int>(repertoire::Departure::You), tr("You left it"), "you"},
          std::tuple{static_cast<int>(repertoire::Departure::Opponent), tr("They left it"),
                     "opponent"},
          std::tuple{static_cast<int>(repertoire::Departure::End), tr("Book ended"), "end"}}) {
        auto* button = chip(text, QStringLiteral("profileTheoryKind:") + QString::fromLatin1(name));
        button->setChecked(kind == -1);
        connect(button, &QPushButton::clicked, this, [this, which = kind] {
            m_theoryKind = which;
            fillTheory();
        });
        kinds->addWidget(button);
    }
    kinds->addStretch();
    theoryBox->addLayout(kinds);
    m_theorySummary = label(QString(), "emptyHint");
    m_theorySummary->setObjectName(QStringLiteral("profileTheorySummary"));
    m_theorySummary->setWordWrap(true);
    theoryBox->addWidget(m_theorySummary);
    m_theory = new QTreeWidget;
    m_theory->setObjectName(QStringLiteral("profileTheory"));
    m_theory->setColumnCount(4);
    m_theory->setHeaderLabels({tr("OPENING · MOVE"), tr("WHO"), tr("GAMES"), tr("THE BOOK")});
    m_theory->setUniformRowHeights(true);
    m_theory->setFrameShape(QFrame::NoFrame);
    m_theory->header()->setStretchLastSection(true);
    m_theory->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_theory->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_theory->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_theory->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_theory->setSortingEnabled(true);
    m_theory->sortByColumn(2, Qt::DescendingOrder); // the most games first
    sortOnClicksThenReset(m_theory->header(), 2, Qt::DescendingOrder);
    m_theory->setToolTip(tr("Double-click a move to open your repertoire there, a game to open "
                            "it at that move"));
    connect(m_theory, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (!item->data(0, kGroupRole).isValid()) {
            return; // an opening: it opens and closes
        }
        const int group = item->data(0, kGroupRole).toInt();
        if (group < 0 || std::cmp_greater_equal(group, m_theoryReport.groups.size())) {
            return;
        }
        const repertoire::DepartureGroup& departure =
            m_theoryReport.groups[static_cast<std::size_t>(group)];
        const QVariant game = item->data(0, kGameRole);
        if (!game.isValid()) {
            emit repertoireRequested(departure.side, departure.line);
            return;
        }
        const auto index = static_cast<std::size_t>(game.toInt());
        if (index < m_theoryGames.size() && index < m_theorySources.size()) {
            if (const auto whole = m_theorySources[index]->game(m_theoryGames[index].id)) {
                const int ply = static_cast<int>(departure.line.size()) + 1;
                m_store.originalGame(m_current, m_part, *whole,
                                     [this, ply](const core::Game& original) {
                                         emit openGameAtRequested(original, ply);
                                     });
            }
        }
    });
    theoryBox->addWidget(m_theory, 1);
    m_pages->addWidget(theoryPage);

    auto* panel = new QVBoxLayout;
    panel->setSpacing(12);
    panel->addLayout(panelTop);
    panel->addWidget(m_pages, 1);
    auto* body = new QHBoxLayout;
    body->setSpacing(18);
    body->addWidget(accountsScroll);
    body->addLayout(panel, 1);

    auto* detail = card();
    auto* box = new QVBoxLayout(detail);
    box->setContentsMargins(layout::kCard);
    box->setSpacing(layout::kCardSpacing);
    box->addLayout(top);
    box->addWidget(m_status);
    box->addSpacing(4);
    box->addLayout(body, 1);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(listCard);
    splitter->addWidget(detail);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({230, 950});
    layout->addWidget(splitter, 1);

    connect(&m_store, &ProfileStore::changed, this, &ProfilesView::refreshList);
    connect(&m_repertoires, &RepertoireStore::openingsReady, this, &ProfilesView::fillTheory);
    connect(&m_repertoires, &RepertoireStore::changed, this, [this] {
        if (m_pages->currentIndex() == 2) {
            showTheory(); // a file marked as a repertoire, or changed: check again
        }
    });
    connect(&m_lichess, &LichessLogin::changed, this, &ProfilesView::showProfile);
    connect(&m_lichess, &LichessLogin::signedInNow, this,
            [this](const QString& name, const QString& wanted) {
                m_signingInFor.clear();
                // Asked for one account, signed in as another (logged in as that one on Lichess's
                // page): both fine, but say so.
                if (!wanted.isEmpty() && wanted.compare(name, Qt::CaseInsensitive) != 0) {
                    notice(this, tr("Lichess"),
                           tr("Kiebitz is signed in as %1 now (not %2): that's the "
                              "account you logged in to on Lichess.")
                               .arg(name, wanted));
                }
                // An account no profile has yet goes to yours (its games come with it).
                const bool known = std::any_of(
                    m_store.profiles().begin(), m_store.profiles().end(),
                    [&](const Profile& profile) {
                        return std::any_of(profile.accounts.begin(), profile.accounts.end(),
                                           [&](const Account& account) {
                                               return account.site == AccountSite::Lichess &&
                                                      account.username.compare(
                                                          name, Qt::CaseInsensitive) == 0;
                                           });
                    });
                if (!known) {
                    for (const Profile& profile : m_store.profiles()) {
                        if (profile.main) {
                            m_store.addAccount(profile.id, AccountSite::Lichess, name);
                        }
                    }
                }
            });
    connect(&m_store, &ProfileStore::progress, this, [this](const QString& id) {
        if (id == m_current) {
            const QString status = m_store.status(id);
            m_status->setText(status);
            m_status->setVisible(!status.isEmpty());
            m_sync->setEnabled(status.isEmpty());
        }
    });
    connect(&m_store, &ProfileStore::failed, this, [this](const QString&, const QString& message) {
        notice(this, tr("Profiles"), message);
    });
    // Over-the-board games added: say how many, when some were there already.
    connect(
        &m_store, &ProfileStore::otbAdded, this, [this](const QString& id, int added, int skipped) {
            if (id != m_current || skipped == 0) {
                return;
            }
            const QString already =
                skipped == 1
                    ? tr("1 game was already in this profile and was left out.")
                    : tr("%1 games were already in this profile and were left out.").arg(skipped);
            QString news = tr("%1 new games added.").arg(added);
            if (added == 0) {
                news = tr("No new games.");
            } else if (added == 1) {
                news = tr("1 new game added.");
            }
            notice(this, tr("Add Over-the-Board Games"), news + QLatin1Char(' ') + already);
        });
    refreshList();
}

void ProfilesView::select(const QString& profileId) {
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(kIdRole).toString() == profileId) {
            m_list->setCurrentRow(i);
        }
    }
}

void ProfilesView::refreshList() {
    const QString keep = m_current;
    {
        const QSignalBlocker block(m_list);
        m_list->clear();
        const auto& c = m_style.colors();
        for (const Profile& profile : m_store.profiles()) {
            auto* item = new QListWidgetItem(m_list);
            item->setData(kIdRole, profile.id);
            auto* row = new QWidget;
            auto* rowBox = new QHBoxLayout(row);
            rowBox->setContentsMargins(10, 8, 10, 8);
            rowBox->setSpacing(10);
            auto* icon = new QLabel;
            icon->setPixmap(makeIcon(IconKind::Person, c.muted, c.dimmed, 18).pixmap(18, 18));
            rowBox->addWidget(icon);
            auto* texts = new QVBoxLayout;
            texts->setSpacing(1);
            // Long names end in "…" (the full name is the tooltip).
            auto* name = new ElidedLabel;
            name->setProperty("role", "databaseName");
            name->setFullText(profile.main ? tr("%1 (you)").arg(profile.name) : profile.name);
            texts->addWidget(name);
            texts->addWidget(
                label(gamesText(profile.all ? profile.all->gameCount() : 0), "emptyHint"));
            rowBox->addLayout(texts, 1);
            item->setSizeHint(QSize(0, row->sizeHint().height())); // as wide as the list
            m_list->setItemWidget(item, row);
        }
    }
    m_header->setSubtitle(m_store.profiles().size() == 1
                              ? tr("1 profile")
                              : tr("%1 profiles").arg(m_store.profiles().size()));
    m_current = keep;
    int row = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(kIdRole).toString() == keep) {
            row = i;
        }
    }
    {
        const QSignalBlocker block(m_list);
        m_list->setCurrentRow(row);
    }
    if (const auto* item = m_list->currentItem()) {
        m_current = item->data(kIdRole).toString();
    }
    showProfile();
}

void ProfilesView::showProfile() {
    const Profile* profile = m_store.profile(m_current);
    if (profile == nullptr) {
        return;
    }
    m_name->setText(profile->name);
    const auto& colors = m_style.colors();
    m_info->setIcon(
        makeIcon(IconKind::Info, profile->fide ? colors.accent : colors.muted, colors.dimmed, 16));
    const quint64 games = profile->all ? profile->all->gameCount() : 0;
    QStringList summary{gamesText(games), profile->accounts.size() == 1
                                              ? tr("1 account")
                                              : tr("%1 accounts").arg(profile->accounts.size())};
    if (profile->fide) { // e.g. "GM · NOR · FIDE 2823"
        QStringList fide;
        if (!profile->fide->title.isEmpty()) {
            fide << profile->fide->title;
        }
        if (!profile->fide->federation.isEmpty()) {
            fide << profile->fide->federation;
        }
        if (profile->fide->standard > 0) {
            fide << tr("FIDE %1").arg(profile->fide->standard);
        }
        if (!fide.isEmpty()) {
            summary << fide.join(QStringLiteral(" · "));
        }
    }
    m_summary->setText(summary.join(QStringLiteral("  ·  ")));
    m_remove->setVisible(!profile->main);
    const QString status = m_store.status(m_current);
    m_status->setText(status);
    m_status->setVisible(!status.isEmpty());
    m_sync->setEnabled(status.isEmpty() && !profile->accounts.empty());
    showAccounts(*profile);
    showParts(*profile);
    showGames();
}

void ProfilesView::showAccounts(const Profile& profile) {
    while (const QLayoutItem* item = m_accounts->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto& c = m_style.colors();
    const auto partGames = [&](const QString& part) -> quint64 {
        const auto found = profile.parts.find(part);
        return found != profile.parts.end() ? found->second->gameCount() : 0;
    };
    // A card: a head (badge, name, a button), then rows of a caption and a value.
    const auto newCard = [&](const QString& badge, const QString& name, QPushButton* action) {
        auto* frame = new QFrame;
        frame->setProperty("statTile", true);
        frame->setProperty("accountCard", true);
        auto* frameBox = new QVBoxLayout(frame);
        frameBox->setContentsMargins(14, 12, 12, 12);
        frameBox->setSpacing(6);
        auto* head = new QHBoxLayout;
        head->setSpacing(8);
        auto* siteBadge = label(badge, "siteBadge");
        siteBadge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        head->addWidget(siteBadge, 0, Qt::AlignVCenter);
        auto* title = label(name, "databaseName");
        title->setTextFormat(Qt::RichText);
        head->addWidget(title, 1);
        if (action != nullptr) {
            head->addWidget(action);
        }
        frameBox->addLayout(head);
        frameBox->addSpacing(2);
        m_accounts->addWidget(frame);
        return frameBox;
    };
    const auto row = [](QVBoxLayout* box, const QString& caption, QWidget* value,
                        QWidget* extra = nullptr) {
        auto* line = new QHBoxLayout;
        line->setSpacing(6);
        line->addWidget(label(caption, "emptyHint"), 1);
        line->addWidget(value);
        if (extra != nullptr) {
            line->addWidget(extra);
        }
        box->addLayout(line);
    };
    const auto gamesRow = [&](QVBoxLayout* box, quint64 have, quint64 total) {
        row(box, tr("Games"),
            label(total > have ? tr("%1 of %2").arg(number(have), number(total)) : number(have),
                  "ratingValue"));
        if (total > 0) {
            auto* bar = new QProgressBar;
            bar->setObjectName(QStringLiteral("accountProgress"));
            bar->setTextVisible(false);
            bar->setFixedHeight(4);
            bar->setRange(0, 1000);
            bar->setValue(static_cast<int>(std::min<quint64>(have, total) * 1000 / total));
            box->addWidget(bar);
        }
    };

    if (profile.main &&
        std::none_of(profile.accounts.begin(), profile.accounts.end(),
                     [](const Account& account) { return account.site == AccountSite::Lichess; })) {
        showLichessLogin();
    }
    for (const Account& account : profile.accounts) {
        auto* remove = new QPushButton;
        remove->setProperty("role", "nav");
        remove->setToolTip(tr("Remove this account and its games"));
        remove->setIcon(makeIcon(IconKind::Trash, c.muted, c.dimmed, 14));
        remove->setFixedSize(24, 24);
        connect(remove, &QPushButton::clicked, this,
                [this, id = account.id, name = account.username] {
                    if (confirm(this, tr("Remove Account"),
                                tr("Remove %1 and its games from this profile?").arg(name),
                                tr("Remove"), MessageDialog::Action::Destructive)) {
                        m_store.removeAccount(m_current, id);
                    }
                });
        const QString name = account.title.isEmpty()
                                 ? account.username.toHtmlEscaped()
                                 : QStringLiteral("<span style=\"color:%1\">%2</span> %3")
                                       .arg(c.accent.name(), account.title.toHtmlEscaped(),
                                            account.username.toHtmlEscaped());
        QVBoxLayout* box = newCard(siteText(account.site).toUpper(), name, remove);
        // The site's ratings, fastest first, with how each moved at the last sync.
        for (const auto& [speed, caption] :
             {std::pair{"bullet", tr("Bullet")}, std::pair{"blitz", tr("Blitz")},
              std::pair{"rapid", tr("Rapid")}, std::pair{"classical", tr("Classical")},
              std::pair{"daily", tr("Daily")}, std::pair{"correspondence", tr("Correspondence")}}) {
            const auto found = account.ratings.find(QString::fromLatin1(speed));
            if (found == account.ratings.end()) {
                continue;
            }
            QLabel* change = nullptr;
            if (const auto moved = account.ratingChanges.find(found->first);
                moved != account.ratingChanges.end() && moved->second != 0) {
                change = label(moved->second > 0 ? QStringLiteral("↗ %1").arg(moved->second)
                                                 : QStringLiteral("↘ %1").arg(-moved->second),
                               "ratingChange");
                change->setProperty("mood", moved->second > 0 ? "good" : "bad");
            }
            row(box, caption, label(QString::number(found->second), "ratingValue"), change);
        }
        gamesRow(box, partGames(account.id), static_cast<quint64>(std::max(account.siteGames, 0)));
        // "synced 2 h ago", and for Lichess: signed in or not, with what to do about it.
        auto* foot = new QHBoxLayout;
        foot->setSpacing(6);
        foot->addWidget(label(ago(account.synced), "cardFoot"), 1);
        if (account.site == AccountSite::Lichess) {
            auto* action = new QPushButton;
            action->setProperty("role", "link");
            action->setFocusPolicy(Qt::NoFocus);
            action->setObjectName(QStringLiteral("lichessSignIn:") + account.username);
            if (m_lichess.signedIn(account.username)) {
                foot->addWidget(label(tr("Signed in"), "cardFoot"));
                action->setText(tr("Sign Out"));
                connect(action, &QPushButton::clicked, this,
                        [this, name = account.username] { m_lichess.signOut(name); });
            } else if (m_lichess.busy() &&
                       m_signingInFor.compare(account.username, Qt::CaseInsensitive) == 0) {
                foot->addWidget(label(tr("Waiting for your browser…"), "cardFoot"));
                action->setText(tr("Cancel"));
                connect(action, &QPushButton::clicked, &m_lichess, &LichessLogin::cancel);
            } else {
                action->setText(tr("Sign In"));
                action->setToolTip(tr("For faster downloads, and Lichess's opening explorer. "
                                      "Your browser opens Lichess's sign-in page."));
                connect(action, &QPushButton::clicked, this, [this, name = account.username] {
                    m_signingInFor = name;
                    m_lichess.signIn(name); // that account: Lichess asks for its login if need be
                });
            }
            foot->addWidget(action);
        }
        box->addLayout(foot);
    }

    auto* addOtb = new QPushButton(tr("Add PGN…"));
    addOtb->setObjectName(QStringLiteral("profileAddOtb"));
    addOtb->setProperty("role", "secondary");
    connect(addOtb, &QPushButton::clicked, this, &ProfilesView::addOtbGames);
    QVBoxLayout* otb = newCard(tr("OTB"), tr("Over the board"), addOtb);
    gamesRow(otb, partGames(QString::fromLatin1(kOtb)), 0);
    otb->addWidget(label(tr("Your tournament games, from PGN files"), "cardFoot"));

    auto* addAccountButton = new QPushButton(tr("Add Account  +"));
    addAccountButton->setObjectName(QStringLiteral("profileAddAccount"));
    addAccountButton->setProperty("role", "secondary");
    addAccountButton->setMinimumHeight(34);
    connect(addAccountButton, &QPushButton::clicked, this, &ProfilesView::addAccount);
    m_accounts->addWidget(addAccountButton);
    m_accounts->addStretch();
}

void ProfilesView::showLichessLogin() {
    auto* frame = new QFrame;
    frame->setProperty("statTile", true);
    frame->setObjectName(QStringLiteral("lichessLogin"));
    auto* box = new QVBoxLayout(frame);
    box->setContentsMargins(14, 12, 12, 12);
    box->setSpacing(6);
    auto* head = new QHBoxLayout;
    head->setSpacing(8);
    auto* badge = label(tr("LICHESS"), "siteBadge");
    badge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    head->addWidget(badge, 0, Qt::AlignVCenter);
    auto* button = new QPushButton;
    button->setObjectName(QStringLiteral("lichessSignIn"));
    QString title;
    QString foot;
    if (m_lichess.busy()) {
        title = tr("Waiting for your browser…");
        foot = tr("Allow Kiebitz on the Lichess page that opened in your browser. Lichess then "
                  "sends the browser back to Kiebitz (a page from 127.0.0.1, this Mac).");
        button->setText(tr("Cancel"));
        button->setProperty("role", "secondary");
        connect(button, &QPushButton::clicked, &m_lichess, &LichessLogin::cancel);
    } else {
        title = tr("Sign in with Lichess");
        foot = tr("For Lichess's opening explorer, and faster downloads of your games.");
        button->setText(tr("Sign In"));
        button->setProperty("role", "primary");
        connect(button, &QPushButton::clicked, this, [this] {
            m_signingInFor.clear();
            m_lichess.signIn({});
        });
    }
    button->setProperty("compact", true);
    auto* titleLabel = label(title, "databaseName");
    titleLabel->setWordWrap(true); // the column is narrow: never wider than it
    head->addWidget(titleLabel, 1);
    head->addWidget(button, 0, Qt::AlignTop);
    box->addLayout(head);
    auto* hint = label(foot, "cardFoot");
    hint->setWordWrap(true);
    box->addWidget(hint);
    m_accounts->addWidget(frame);
}

void ProfilesView::showParts(const Profile& profile) {
    const QSignalBlocker block(m_parts);
    m_parts->clear();
    m_parts->addItem(tr("All games"), QString());
    for (const Account& account : profile.accounts) {
        m_parts->addItem(QStringLiteral("%1 · %2").arg(siteText(account.site), account.username),
                         account.id);
    }
    m_parts->addItem(tr("Over the board"), QString::fromLatin1(kOtb));
    const int index = m_parts->findData(m_part);
    m_parts->setCurrentIndex(index >= 0 ? index : 0);
    m_part = m_parts->currentData().toString(); // gone (another profile, an account removed)
}

void ProfilesView::showGames() {
    const Profile* profile = m_store.profile(m_current);
    m_db.reset();
    if (profile != nullptr) {
        if (m_part.isEmpty()) {
            m_db = profile->all;
        } else if (const auto found = profile->parts.find(m_part); found != profile->parts.end()) {
            m_db = found->second;
        }
    }
    m_model->setDatabase(m_db);
    showStats();
    if (m_pages->currentIndex() == 2) {
        showTheory();
    }
}

void ProfilesView::showStats() {
    const Profile* profile = m_store.profile(m_current);
    std::vector<std::string> names;
    if (profile != nullptr) {
        for (const QString& name : ProfileStore::namesOf(*profile, m_part)) {
            names.push_back(name.toStdString());
        }
    }
    const int request = ++m_statsRequest;
    // Owned by the view and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher = new QFutureWatcher<gamedb::PlayerStats>(
        this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(
        watcher, &QFutureWatcher<gamedb::PlayerStats>::finished, this, [this, watcher, request] {
            watcher->deleteLater();
            if (request != m_statsRequest) {
                return;
            }
            const gamedb::PlayerStats stats = watcher->result();
            const auto& total = stats.total;
            std::array<std::uint64_t, 3> both{};
            for (std::size_t k = 0; k < 3; ++k) {
                both[k] = total.asWhite[k] + total.asBlack[k];
            }
            const std::uint64_t games = both[0] + both[1] + both[2];
            m_totalText->setText(
                games == 0 ? tr("No games")
                           : QStringLiteral("%1  ·  %2")
                                 .arg(gamesText(games), scoreText(both[0], both[1], both[2])));
            m_totalBar->setResults(both[0], both[1], both[2]);
            const auto& c = m_style.colors();
            QStringList legend;
            for (const auto& [count, caption, color] : {std::tuple{both[0], tr("Wins"), c.good},
                                                        std::tuple{both[1], tr("Draws"), c.draws},
                                                        std::tuple{both[2], tr("Losses"), c.bad}}) {
                const int share =
                    games == 0
                        ? 0
                        : qRound(100.0 * static_cast<double>(count) / static_cast<double>(games));
                legend << QStringLiteral("<span style=\"color:%1\">●</span>&nbsp; %2 "
                                         "<b style=\"color:%3\">%4</b> &nbsp;%5%")
                              .arg(color.name(), caption, c.text.name(), number(count))
                              .arg(share);
            }
            m_legend->setText(legend.join(QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;")));
            const auto side = [&](const std::array<std::uint64_t, 3>& counts, QLabel* text,
                                  ResultBar* bar) {
                text->setText(QStringLiteral("%1  ·  %2")
                                  .arg(scoreText(counts[0], counts[1], counts[2]),
                                       gamesText(counts[0] + counts[1] + counts[2])));
                bar->setResults(counts[0], counts[1], counts[2]);
            };
            side(total.asWhite, m_whiteText, m_whiteBar);
            side(total.asBlack, m_blackText, m_blackBar);
            m_activity->setYears(stats.years);
        });
    watcher->setFuture(QtConcurrent::run([db = m_db, names = std::move(names), speeds = m_speeds] {
        return db ? db->playerStats(names, speeds) : gamedb::PlayerStats{};
    }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

void ProfilesView::showTheory() {
    const Profile* profile = m_store.profile(m_current);
    const BuiltRepertoire* white = m_repertoires.get(core::Color::White);
    const BuiltRepertoire* black = m_repertoires.get(core::Color::Black);
    const int request = ++m_theoryRequest;
    m_theoryReport = {};
    m_theoryGames.clear();
    m_theorySources.clear();
    m_theory->clear();
    if (profile == nullptr || !m_db) {
        m_theorySummary->setText(tr("No games here yet."));
        emit theoryChecked();
        return;
    }
    if ((white == nullptr || white->tree.empty()) && (black == nullptr || black->tree.empty())) {
        m_theorySummary->setText(
            tr("No repertoire yet. Set a file's type to Repertoire (right-click "
               "it in the Library) to check these games against it."));
        emit theoryChecked();
        return;
    }
    // Saved from before (in memory, else on disk): shown at once; checked again only if the
    // repertoire files, the games or the names have changed since.
    const QString key = m_current + u'|' + m_part;
    const QString file = m_store.cacheFile(
        m_current,
        QStringLiteral("theory-%1.json").arg(m_part.isEmpty() ? QStringLiteral("all") : m_part));
    if (!m_theorySaved.contains(key)) {
        QFile saved(file);
        if (saved.open(QIODevice::ReadOnly)) {
            if (auto read = theoryFromJson(QJsonDocument::fromJson(saved.readAll()).object())) {
                auto& [fingerprint, report, games] = *read;
                m_theorySaved[key] = {fingerprint, std::move(report), std::move(games)};
            }
        }
    }
    const QString fingerprint = theoryFingerprint(*profile);
    bool stale = false;
    if (const auto found = m_theorySaved.find(key); found != m_theorySaved.end()) {
        m_theoryReport = found->second.report;
        m_theoryGames = found->second.games;
        stale = found->second.fingerprint != fingerprint;
        if (!stale) {
            m_theorySources.assign(m_theoryGames.size(), m_db);
        } // else its games may not be where they were: not opened until checked again
        fillTheory();
        if (!stale) {
            emit theoryChecked();
            return;
        }
    }
    // A repertoire still being read: checked once it is there (its change brings us back).
    if (m_repertoires.isBuilding(core::Color::White) ||
        m_repertoires.isBuilding(core::Color::Black)) {
        if (!stale) {
            m_theorySummary->setText(tr("Reading your repertoire…"));
        }
        return;
    }
    if (stale) {
        m_theorySummary->setText(m_theorySummary->text() + QLatin1Char(' ') +
                                 tr("Updating, as games or the repertoire changed…"));
    } else {
        m_theorySummary->setText(tr("Checking the games against your repertoire…"));
    }
    QStringList names; // who the profile is in these games, in lower case
    for (const QString& name : ProfileStore::namesOf(*profile, m_part)) {
        names << name.toLower();
    }
    struct Checked {
        repertoire::TheoryReport report;
        std::vector<gamedb::GameInfo> games;
    };
    // Owned by the view and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher =
        new QFutureWatcher<Checked>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<Checked>::finished, this,
            [this, watcher, request, key, file, fingerprint] {
                watcher->deleteLater();
                if (request != m_theoryRequest) {
                    return;
                }
                Checked checked = watcher->result();
                m_theoryReport = std::move(checked.report);
                m_theoryGames = std::move(checked.games);
                m_theorySources.assign(m_theoryGames.size(), m_db);
                // Kept for next time, here and in the profile's folder.
                m_theorySaved[key] = {fingerprint, m_theoryReport, m_theoryGames};
                QSaveFile out(file);
                if (out.open(QIODevice::WriteOnly)) {
                    out.write(
                        QJsonDocument(theoryToJson(fingerprint, m_theoryReport, m_theoryGames))
                            .toJson(QJsonDocument::Compact));
                    out.commit();
                }
                fillTheory();
                emit theoryChecked();
            });
    // From copies: the repertoire may be rebuilt meanwhile.
    watcher->setFuture(QtConcurrent::run(
        [db = m_db, names,
         whiteTree = white != nullptr ? white->tree : repertoire::Tree(core::Color::White),
         blackTree = black != nullptr ? black->tree : repertoire::Tree(core::Color::Black)] {
            Checked result;
            std::vector<repertoire::CheckedGame> games;
            const auto all = db->games(1, static_cast<std::size_t>(db->gameCount()));
            for (const gamedb::GameInfo& info : all) {
                // The profile's colour: the side with one of its names.
                const bool asWhite = names.contains(QString::fromStdString(info.white).toLower());
                const bool asBlack = names.contains(QString::fromStdString(info.black).toLower());
                if (asWhite == asBlack) {
                    continue; // neither (or both): not theirs to judge
                }
                const auto game = db->game(info.id);
                if (!game || game->startPosition().hash() != core::Position().hash()) {
                    continue;
                }
                repertoire::CheckedGame checkedGame;
                checkedGame.side = asWhite ? core::Color::White : core::Color::Black;
                for (const core::NodeId node : game->mainLine()) {
                    if (node != core::Game::root()) {
                        checkedGame.line.push_back(game->node(node).move);
                    }
                }
                games.push_back(std::move(checkedGame));
                result.games.push_back(info);
            }
            result.report = repertoire::checkTheory(whiteTree, blackTree, games);
            return result;
        }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

QString ProfilesView::theoryFingerprint(const Profile& profile) const {
    // What the check is worked out from: the games (the database as built), the repertoire's
    // files (each one's text) and who the profile is in its games.
    QStringList parts{m_store.databaseStamp(profile.id, m_part)};
    for (const core::Color side : {core::Color::White, core::Color::Black}) {
        const BuiltRepertoire* built = m_repertoires.get(side);
        if (built == nullptr) {
            parts << QStringLiteral("-");
            continue;
        }
        for (const RepertoireFile& file : built->files) {
            parts << QStringLiteral("%1:%2").arg(file.file->path).arg(file.textHash);
        }
    }
    parts << ProfileStore::namesOf(profile, m_part).join(u'|').toLower();
    return QString::fromLatin1(
        QCryptographicHash::hash(parts.join(u'\n').toUtf8(), QCryptographicHash::Sha1).toHex());
}

void ProfilesView::fillTheory() {
    const repertoire::TheoryReport& report = m_theoryReport;
    m_theory->setSortingEnabled(false);
    m_theory->clear();
    const auto& c = m_style.colors();
    std::array<std::size_t, 3> counts{};
    // Grouped by opening: its family's name ("Caro-Kann Defense") where the game left the book.
    const repertoire::OpeningNames* names = m_repertoires.openings();
    if (names == nullptr) {
        m_repertoires.loadOpenings(); // fills in again when they're there
    }
    std::map<QString, SortableItem*> openings;
    const auto openingOf = [&](const repertoire::DepartureGroup& group) {
        auto* found = static_cast<SortableItem*>(nullptr);
        QString name = tr("Other openings");
        if (names != nullptr) {
            std::vector<core::Move> moves = group.line;
            moves.push_back(group.played);
            if (const repertoire::Opening* opening = names->along(core::Position(), moves)) {
                name = QString::fromStdString(opening->name).section(u':', 0, 0).trimmed();
            }
        }
        if (const auto it = openings.find(name); it != openings.end()) {
            found = it->second;
        } else {
            found = new SortableItem(m_theory);
            found->setText(0, name);
            QFont bold = found->font(0);
            bold.setWeight(QFont::DemiBold);
            found->setFont(0, bold);
            found->setSortValue(2, 0);
            openings.emplace(name, found);
        }
        // Its games, summed up.
        const double games = found->data(2, SortableItem::kSortRole).toDouble() +
                             static_cast<double>(group.games.size());
        found->setSortValue(2, games);
        found->setText(2, number(static_cast<std::uint64_t>(games)));
        return found;
    };
    for (std::size_t g = 0; g < report.groups.size(); ++g) {
        const repertoire::DepartureGroup& group = report.groups[g];
        counts[static_cast<std::size_t>(group.kind)] += group.games.size();
        if (m_theoryKind >= 0 && static_cast<int>(group.kind) != m_theoryKind) {
            continue;
        }
        // "7. f3", "7… h6"; the line in the tooltip.
        const auto moveText = [](std::size_t ply, const std::string& san) {
            return QStringLiteral("%1%2 %3")
                .arg((ply / 2) + 1)
                .arg(ply % 2 == 0 ? QStringLiteral(".") : QStringLiteral("…"))
                .arg(QString::fromStdString(san));
        };
        auto* item = new SortableItem(openingOf(group));
        item->setData(0, kGroupRole, static_cast<int>(g));
        const std::size_t ply = group.line.size();
        QStringList line; // "1. e4", "e5", "2. Nf3", ...: as a score sheet writes it
        QString last;     // the last move on its own: "1… e5"
        core::Position position;
        for (std::size_t k = 0; k < ply; ++k) {
            const std::string san = position.san(group.line[k]);
            line << (k % 2 == 0 ? moveText(k, san) : QString::fromStdString(san));
            last = moveText(k, san);
            position.play(group.line[k]);
        }
        // Where it left the book; for the book's end, the last move it had.
        if (group.kind == repertoire::Departure::End) {
            item->setText(0, ply == 0 ? tr("at the start") : tr("after %1").arg(last));
        } else {
            item->setText(0, moveText(ply, group.san));
        }
        item->setSortValue(0, static_cast<double>(ply));
        item->setToolTip(0, line.join(u' '));
        switch (group.kind) {
        case repertoire::Departure::You:
            item->setText(1, tr("You"));
            item->setForeground(1, c.bad);
            break;
        case repertoire::Departure::Opponent:
            item->setText(1, tr("They"));
            item->setForeground(1, c.accent);
            break;
        case repertoire::Departure::End:
            item->setText(1, tr("Book ended"));
            item->setForeground(1, c.muted);
            break;
        }
        item->setText(2, number(group.games.size()));
        item->setSortValue(2, static_cast<double>(group.games.size()));
        QStringList expected;
        for (const std::string& san : group.expected) {
            expected << moveText(ply, san);
        }
        item->setText(3, group.kind == repertoire::Departure::Opponent
                             ? tr("covers %1").arg(expected.join(QStringLiteral(", ")))
                             : expected.join(tr(" or ")));
        item->setForeground(3, c.muted);
        // Its games: the date, the opponent, the result; double-click opens one there.
        for (const std::size_t index : group.games) {
            if (index >= m_theoryGames.size()) {
                continue;
            }
            const gamedb::GameInfo& game = m_theoryGames[index];
            const bool white = group.side == core::Color::White;
            const QString opponent = QString::fromStdString(white ? game.black : game.white);
            auto* child = new SortableItem(item);
            child->setData(0, kGroupRole, static_cast<int>(g));
            child->setData(0, kGameRole, static_cast<int>(index));
            child->setText(0, QStringLiteral("%1  ·  %2 %3")
                                  .arg(QString::fromStdString(game.date),
                                       white ? tr("with White against") : tr("with Black against"),
                                       opponent));
            child->setText(1, resultText(game.result));
            child->setForeground(0, c.muted);
            child->setForeground(1, c.muted);
        }
    }
    m_theory->setSortingEnabled(true);
    m_theory->expandToDepth(0); // the openings open, their games not
    if (report.checked == 0) {
        m_theorySummary->setText(tr("None of these games has a colour you have a repertoire for."));
        return;
    }
    m_theorySummary->setText(
        tr("%1 games checked: you left the book in %2, your opponents in %3, the book ended in %4, "
           "and %5 stayed in it. On average the games were in the book until move %6.")
            .arg(number(report.checked), number(counts[0]), number(counts[1]), number(counts[2]),
                 number(report.stayed))
            .arg(qRound(report.averageBookMoves) + 1));
}

QStringList ProfilesView::theoryRows() const {
    QStringList rows;
    for (int i = 0; i < m_theory->topLevelItemCount(); ++i) {
        const auto* opening = m_theory->topLevelItem(i);
        for (int k = 0; k < opening->childCount(); ++k) {
            const auto* item = opening->child(k);
            rows << QStringLiteral("%1: %2  %3  %4  %5")
                        .arg(opening->text(0), item->text(0), item->text(1), item->text(2),
                             item->text(3));
        }
    }
    return rows;
}

QString ProfilesView::theorySummary() const {
    return m_theorySummary->text();
}

void ProfilesView::deleteSelectedGame() {
    const gamedb::GameInfo* info = m_model->at(m_games->currentIndex().row());
    if (info == nullptr || !m_db) {
        return;
    }
    const auto game = m_db->game(info->id);
    if (!game) {
        return;
    }
    const QString what =
        QStringLiteral("%1 – %2%3")
            .arg(QString::fromStdString(info->white), QString::fromStdString(info->black),
                 info->date.empty()
                     ? QString()
                     : QStringLiteral(", %1").arg(QString::fromStdString(info->date)));
    if (confirm(this, tr("Delete Game"), tr("Delete %1 from this profile?").arg(what), tr("Delete"),
                MessageDialog::Action::Destructive,
                tr("It's taken out of the profile's PGN files. If the game is in there twice, "
                   "the other copy stays."))) {
        m_store.deleteGame(m_current, m_part, *game);
    }
}

void ProfilesView::showFideCard() {
    const Profile* profile = m_store.profile(m_current);
    if (profile == nullptr) {
        return;
    }
    QString status;
    bool retry = false;
    if (profile->name.split(u' ', Qt::SkipEmptyParts).size() < 2) {
        status = tr("Give the profile a full name (first and last) to find it in FIDE's rating "
                    "list.");
    } else if (profile->fideLooking) {
        status = tr("Looking it up in FIDE's rating list…");
    } else if (!profile->fideError.isEmpty()) {
        status = profile->fideError;
        retry = true;
    } else if (!profile->fideLooked) {
        status = tr("Looking it up in FIDE's rating list…");
        m_store.lookUpFide(m_current);
    } else if (profile->fideChosen) {
        status = tr("No FIDE player chosen for this profile.");
    } else {
        status = tr("No player called %1 in FIDE's rating list.").arg(profile->name);
        retry = true;
    }
    auto* card = new FideCard(profile->fide, profile->fideCandidates, status, retry,
                              profile->fideChosen, profile->main, m_style, this);
    connect(card, &FideCard::retryRequested, this,
            [this, id = m_current] { m_store.lookUpFide(id); });
    connect(card, &FideCard::chosen, this,
            [this, id = m_current](int fideId) { m_store.chooseFide(id, fideId); });
    card->adjustSize();
    card->move(m_info->mapToGlobal(QPoint(0, m_info->height() + 4)));
    card->show();
}

void ProfilesView::newProfile() {
    NameDialog dialog(tr("New Profile"), tr("Create"), tr("A friend, a club mate, an opponent"), {},
                      this);
    if (dialog.exec() == QDialog::Accepted) {
        const QString id = m_store.addProfile(dialog.name());
        select(id);
    }
}

void ProfilesView::addAccount() {
    AddAccountDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    // Only an account the site has: looked up first, then added under the site's spelling.
    const AccountSite site = dialog.site();
    const QString typed = dialog.username();
    const QString siteName =
        site == AccountSite::Lichess ? QStringLiteral("Lichess") : QStringLiteral("chess.com");
    m_status->setText(tr("Looking up %1 on %2…").arg(typed, siteName));
    m_status->show();
    m_store.checkAccount(
        site, typed,
        [this, site, typed, siteName, profileId = m_current](const QString& name,
                                                             const QString& error) {
            m_status->setText(m_store.status(m_current));
            m_status->setVisible(!m_status->text().isEmpty());
            if (!error.isEmpty()) {
                notice(this, tr("Add Account"), error);
                return;
            }
            if (name.isEmpty()) {
                notice(this, tr("Add Account"),
                       tr("There's no %1 account called “%2”.").arg(siteName, typed),
                       site == AccountSite::Lichess
                           ? tr("Check the spelling: it's the name in the "
                                "account's address, lichess.org/@/name.")
                           : tr("Check the spelling: it's the name in the "
                                "account's address, chess.com/member/name."));
                return;
            }
            if (m_store.addAccount(profileId, site, name).isEmpty()) {
                notice(this, tr("Add Account"), tr("%1 is already in this profile.").arg(name));
            }
        });
}

void ProfilesView::addOtbGames() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Over-the-Board Games"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("PGN files (*.pgn)"));
    if (!files.isEmpty()) {
        m_store.addOtbGames(m_current, files);
    }
}

} // namespace kiebitz::app
