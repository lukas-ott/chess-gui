#include "DatabasesView.hpp"

#include "AddDatabaseDialog.hpp"
#include "DatabaseManager.hpp"
#include "GamesModel.hpp"
#include "style/AppStyle.hpp"
#include "style/Layout.hpp"
#include "style/PageHeader.hpp"

#include <QAbstractTableModel>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <unordered_map>

namespace kiebitz::app {
namespace {

constexpr int kFolderRole = Qt::UserRole;   ///< a database item: its folder
constexpr int kJobRole = Qt::UserRole + 1;  ///< an import item: its job id
constexpr int kNameRole = Qt::UserRole + 2; ///< its name (the row's widget shows it)

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

} // namespace

DatabasesView::DatabasesView(DatabaseManager& manager, const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_manager(manager), m_style(style), m_model(new GamesModel(this)) {
    setObjectName(QStringLiteral("databasesView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(layout::kPage);
    layout->setSpacing(layout::kPageSpacing);
    m_header = new PageHeader(tr("Databases"));
    auto* add = new QPushButton(tr("Add Database"));
    add->setObjectName(QStringLiteral("addDatabase"));
    add->setProperty("role", "primary");
    connect(add, &QPushButton::clicked, this, &DatabasesView::addDatabase);
    m_header->addAction(add);
    layout->addWidget(m_header);

    // The databases, and imports in progress.
    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("databaseList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSpacing(2);
    connect(m_list, &QListWidget::currentRowChanged, this, &DatabasesView::showSelected);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](QPoint at) {
        const QListWidgetItem* item = m_list->itemAt(at);
        if (item == nullptr) {
            return;
        }
        const QString folder = item->data(kFolderRole).toString();
        if (folder.isEmpty()) {
            return;
        }
        QMenu menu(this);
        menu.addAction(
            tr("Move to Trash"), this, [this, folder, name = item->data(kNameRole).toString()] {
                if (QMessageBox::question(this, tr("Move to Trash"),
                                          tr("Move the database “%1” to the Trash?").arg(name)) !=
                    QMessageBox::Yes) {
                    return;
                }
                if (const QString error = m_manager.remove(folder); !error.isEmpty()) {
                    QMessageBox::warning(this, tr("Move to Trash"), error);
                }
            });
        menu.exec(m_list->viewport()->mapToGlobal(at));
    });
    auto* listCard = card();
    auto* listBox = new QVBoxLayout(listCard);
    listBox->setContentsMargins(layout::kCard);
    listBox->addWidget(m_list);

    // The selected database's games.
    m_title = label(QString(), "sectionTitle");
    m_title->setObjectName(QStringLiteral("databaseTitle"));
    m_subtitle = label(QString(), "emptyHint");
    m_subtitle->setObjectName(QStringLiteral("databaseSubtitle"));
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("databaseSearch"));
    m_search->setPlaceholderText(tr("Search players, events, sites, ECO"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(220);
    m_search->setMaximumWidth(300);
    auto* searchDelay = new QTimer(this);
    searchDelay->setSingleShot(true);
    searchDelay->setInterval(250);
    connect(m_search, &QLineEdit::textChanged, searchDelay, qOverload<>(&QTimer::start));
    connect(searchDelay, &QTimer::timeout, this, &DatabasesView::search);
    m_games = new QTableView;
    m_games->setObjectName(QStringLiteral("databaseGames"));
    m_games->setModel(m_model);
    GamesModel::setUpTable(*m_games);
    connect(m_games, &QTableView::doubleClicked, this,
            [this](const QModelIndex& index) { openRow(index.row()); });
    auto* gamesCard = card();
    auto* gamesBox = new QVBoxLayout(gamesCard);
    gamesBox->setContentsMargins(layout::kCard);
    gamesBox->setSpacing(layout::kCardSpacing);
    auto* top = new QHBoxLayout;
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    titles->addWidget(m_title);
    titles->addWidget(m_subtitle);
    top->addLayout(titles, 1);
    top->addWidget(m_search);
    gamesBox->addLayout(top);
    gamesBox->addWidget(m_games, 1);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(listCard);
    splitter->addWidget(gamesCard);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({320, 860});

    // No databases yet.
    auto* empty = new QWidget;
    auto* emptyOuter = new QVBoxLayout(empty);
    auto* emptyCard = card();
    emptyCard->setFixedWidth(460);
    auto* emptyBox = new QVBoxLayout(emptyCard);
    emptyBox->setContentsMargins(28, 26, 28, 26);
    emptyBox->setSpacing(10);
    emptyBox->addWidget(label(tr("No databases yet"), "emptyTitle"));
    auto* hint = label(tr("Add a big collection of games, such as Lumbra's Gigabase or one you "
                          "already have, to search players and openings across millions of "
                          "games."),
                       "emptyHint");
    hint->setWordWrap(true);
    emptyBox->addWidget(hint);
    auto* emptyAdd = new QPushButton(tr("Add Database"));
    emptyAdd->setProperty("role", "primary");
    connect(emptyAdd, &QPushButton::clicked, this, &DatabasesView::addDatabase);
    auto* emptyRow = new QHBoxLayout;
    emptyRow->addWidget(emptyAdd);
    emptyRow->addStretch();
    emptyBox->addSpacing(6);
    emptyBox->addLayout(emptyRow);
    emptyOuter->addStretch(2);
    emptyOuter->addWidget(emptyCard, 0, Qt::AlignHCenter);
    emptyOuter->addStretch(3);

    m_pages = new QStackedWidget;
    m_pages->addWidget(empty);
    m_pages->addWidget(splitter);
    layout->addWidget(m_pages, 1);

    connect(&m_manager, &DatabaseManager::changed, this, &DatabasesView::refreshList);
    connect(&m_manager, &DatabaseManager::jobChanged, this, &DatabasesView::refreshList);
    connect(&m_manager, &DatabaseManager::jobFinished, this, [this](int, const QString& error) {
        if (!error.isEmpty() && error != u"cancelled") {
            QMessageBox::warning(this, tr("Import"), error);
        }
        refreshList();
    });
    refreshList();
}

void DatabasesView::addDatabase() {
    AddDatabaseDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted && !dialog.path().isEmpty()) {
        importFile(dialog.path(), dialog.name(), dialog.source());
    }
}

void DatabasesView::importFile(const QString& path, const QString& name, const QString& source) {
    const qint64 needed = DatabaseManager::spaceNeeded(path);
    const qint64 free = m_manager.spaceFree();
    if (free >= 0 && needed > free) {
        const QLocale locale;
        QMessageBox::warning(
            this, tr("Not enough space"),
            tr("Importing this needs about %1 of disk space, and %2 is free.")
                .arg(locale.formattedDataSize(needed, 1, QLocale::DataSizeSIFormat),
                     locale.formattedDataSize(free, 1, QLocale::DataSizeSIFormat)));
        return;
    }
    m_manager.importFile(path, name.isEmpty() ? QFileInfo(path).completeBaseName() : name, source);
}

void DatabasesView::refreshList() {
    const QString selected = m_list->currentItem() != nullptr
                                 ? m_list->currentItem()->data(kFolderRole).toString()
                                 : m_shown;
    const QLocale locale;
    const auto& databases = m_manager.databases();
    const auto& jobs = m_manager.jobs();
    quint64 games = 0;
    {
        const QSignalBlocker block(m_list);
        m_list->clear();
        for (const InstalledDatabase& database : databases) {
            const auto& info = database.db->info();
            games += database.db->gameCount();
            auto* item = new QListWidgetItem(m_list);
            item->setData(kFolderRole, database.folder);
            item->setData(kNameRole, QString::fromStdString(info.name));
            auto* row = new QWidget;
            auto* box = new QVBoxLayout(row);
            box->setContentsMargins(10, 8, 10, 8);
            box->setSpacing(2);
            box->addWidget(label(QString::fromStdString(info.name), "databaseName"));
            box->addWidget(label(
                tr("%1 games  ·  %2 players  ·  %3")
                    .arg(number(database.db->gameCount()), number(info.players),
                         locale.formattedDataSize(database.bytes, 1, QLocale::DataSizeSIFormat)),
                "emptyHint"));
            item->setSizeHint(row->sizeHint());
            m_list->setItemWidget(item, row);
        }
        for (const DatabaseJob& job : jobs) {
            auto* item = new QListWidgetItem(m_list);
            item->setData(kJobRole, job.id);
            item->setData(kNameRole, job.name);
            item->setFlags(Qt::ItemIsEnabled);
            auto* row = new QWidget;
            auto* box = new QVBoxLayout(row);
            box->setContentsMargins(10, 8, 10, 8);
            box->setSpacing(4);
            auto* head = new QHBoxLayout;
            head->addWidget(label(job.name, "databaseName"), 1);
            auto* cancel = new QPushButton(tr("Cancel"));
            cancel->setObjectName(QStringLiteral("cancelImport"));
            cancel->setProperty("role", "secondary");
            cancel->setFocusPolicy(Qt::NoFocus);
            connect(cancel, &QPushButton::clicked, this,
                    [this, id = job.id] { m_manager.cancel(id); });
            head->addWidget(cancel);
            box->addLayout(head);
            auto* bar = new QProgressBar;
            bar->setObjectName(QStringLiteral("practiceBar")); // the same thin bar
            bar->setTextVisible(false);
            bar->setFixedHeight(4);
            if (job.fraction < 0) {
                bar->setRange(0, 0); // busy: the end isn't known while an archive is unpacked
            } else {
                bar->setRange(0, 1000);
                bar->setValue(static_cast<int>(job.fraction * 1000));
            }
            box->addWidget(bar);
            box->addWidget(label(
                job.games > 0 ? tr("%1  ·  %2 games").arg(job.phase, number(job.games)) : job.phase,
                "emptyHint"));
            item->setSizeHint(row->sizeHint());
            m_list->setItemWidget(item, row);
        }
    }
    m_header->setSubtitle(databases.empty()
                              ? QString()
                              : (databases.size() == 1 ? tr("1 database")
                                                       : tr("%1 databases").arg(databases.size())) +
                                    QStringLiteral("  ·  ") + tr("%1 games").arg(number(games)));
    m_pages->setCurrentIndex(databases.empty() && jobs.empty() ? 0 : 1);
    // The same database selected again (or the first).
    int row = -1;
    for (int i = 0; i < m_list->count(); ++i) {
        if (!selected.isEmpty() && m_list->item(i)->data(kFolderRole).toString() == selected) {
            row = i;
        }
    }
    if (row < 0 && !databases.empty()) {
        row = 0;
    }
    m_list->setCurrentRow(row);
    showSelected();
}

void DatabasesView::showSelected() {
    const QListWidgetItem* item = m_list->currentItem();
    const QString folder = item != nullptr ? item->data(kFolderRole).toString() : QString();
    if (folder == m_shown && (m_db != nullptr || folder.isEmpty())) {
        return; // the same database: keep its search and scroll position
    }
    m_shown = folder;
    m_db.reset();
    for (const InstalledDatabase& database : m_manager.databases()) {
        if (database.folder == folder) {
            m_db = database.db;
        }
    }
    m_search->clear();
    m_model->setDatabase(m_db);
    if (!m_db) {
        m_title->clear();
        m_subtitle->clear();
        return;
    }
    const auto& info = m_db->info();
    m_title->setText(QString::fromStdString(info.name));
    m_subtitle->setText(tr("%1 games  ·  from %2")
                            .arg(number(m_db->gameCount()), QString::fromStdString(info.source)));
}

void DatabasesView::search() {
    if (!m_db) {
        return;
    }
    const QString text = m_search->text().trimmed();
    const int request = ++m_searchRequest;
    if (text.isEmpty()) {
        m_model->setDatabase(m_db);
        return;
    }
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

void DatabasesView::openRow(int row) {
    const gamedb::GameInfo* info = m_model->at(row);
    if (info == nullptr || !m_db) {
        return;
    }
    if (const auto game = m_db->game(info->id)) {
        emit openGameRequested(*game);
    }
}

} // namespace kiebitz::app
