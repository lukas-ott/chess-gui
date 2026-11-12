#include "ProfilesView.hpp"

#include "AddAccountDialog.hpp"
#include "FideCard.hpp"
#include "ProfileStore.hpp"
#include "databases/GamesModel.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"
#include "style/Layout.hpp"
#include "style/PageHeader.hpp"
#include "widgets/ActivityChart.hpp"
#include "widgets/ElidedLabel.hpp"
#include "widgets/NameDialog.hpp"
#include "widgets/ResultBar.hpp"

#include <QComboBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <array>

namespace kiebitz::app {
namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr auto kOtb = "otb";
constexpr int kAccountsWidth = 300;

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

ProfilesView::ProfilesView(ProfileStore& store, const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_store(store), m_style(style), m_model(new GamesModel(this)) {
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
        if (QMessageBox::question(
                this, tr("Remove Profile"),
                tr("Remove “%1” and the games downloaded for it?").arg(profile->name)) ==
            QMessageBox::Yes) {
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
    for (const auto& [index, text, name] : {std::tuple{0, tr("Overview"), "profileShowOverview"},
                                            std::tuple{1, tr("Games"), "profileShowGames"}}) {
        auto* button = new QPushButton(text);
        button->setObjectName(QString::fromLatin1(name));
        button->setProperty("role", "segment");
        button->setCheckable(true);
        button->setChecked(index == 0);
        button->setAutoExclusive(true);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, m_pages,
                [this, index] { m_pages->setCurrentIndex(index); });
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
    connect(searchDelay, &QTimer::timeout, this, &ProfilesView::search);
    m_games = new QTableView;
    m_games->setObjectName(QStringLiteral("profileGames"));
    m_games->setModel(m_model);
    GamesModel::setUpTable(*m_games);
    connect(m_games, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        const gamedb::GameInfo* info = m_model->at(index.row());
        if (info != nullptr && m_db) {
            if (const auto game = m_db->game(info->id)) {
                emit openGameRequested(*game);
            }
        }
    });
    gamesBox->addWidget(m_search);
    gamesBox->addWidget(m_games, 1);
    m_pages->addWidget(gamesPage);

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
    connect(&m_store, &ProfileStore::progress, this, [this](const QString& id) {
        if (id == m_current) {
            const QString status = m_store.status(id);
            m_status->setText(status);
            m_status->setVisible(!status.isEmpty());
            m_sync->setEnabled(status.isEmpty());
        }
    });
    connect(&m_store, &ProfileStore::failed, this, [this](const QString&, const QString& message) {
        QMessageBox::warning(this, tr("Profiles"), message);
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

    for (const Account& account : profile.accounts) {
        auto* remove = new QPushButton;
        remove->setProperty("role", "nav");
        remove->setToolTip(tr("Remove this account and its games"));
        remove->setIcon(makeIcon(IconKind::Trash, c.muted, c.dimmed, 14));
        remove->setFixedSize(24, 24);
        connect(remove, &QPushButton::clicked, this,
                [this, id = account.id, name = account.username] {
                    if (QMessageBox::question(
                            this, tr("Remove Account"),
                            tr("Remove %1 and its games from this profile?").arg(name)) ==
                        QMessageBox::Yes) {
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
        box->addWidget(label(ago(account.synced), "cardFoot"));
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
    if (!m_search->text().trimmed().isEmpty()) {
        search();
    }
    showStats();
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

void ProfilesView::search() {
    const QString text = m_search->text().trimmed();
    if (!m_db || text.isEmpty()) {
        m_model->setDatabase(m_db);
        return;
    }
    const int request = ++m_searchRequest;
    using Found = std::vector<gamedb::GameInfo>;
    // Owned by the view and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher =
        new QFutureWatcher<Found>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<Found>::finished, this, [this, watcher, request] {
        watcher->deleteLater();
        if (request == m_searchRequest) {
            m_model->setResults(watcher->result());
        }
    });
    watcher->setFuture(QtConcurrent::run(
        [db = m_db, words = text.toStdString()] { return db->search(words, 1000); }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

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
    auto* card = new FideCard(profile->fide, profile->fideCandidates, status, retry, m_style, this);
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
    if (dialog.exec() == QDialog::Accepted &&
        m_store.addAccount(m_current, dialog.site(), dialog.username()).isEmpty()) {
        QMessageBox::information(this, tr("Add Account"),
                                 tr("%1 is already in this profile.").arg(dialog.username()));
    }
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
