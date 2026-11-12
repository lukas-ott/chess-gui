#include "GamesModel.hpp"

#include "widgets/SortCycle.hpp"

#include <QFutureWatcher>
#include <QHeaderView>
#include <QLocale>
#include <QTableView>
#include <QtConcurrent/QtConcurrentRun>

#include <array>

namespace kiebitz::app {

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

GamesModel::GamesModel(QObject* parent) : QAbstractTableModel(parent) {}

void GamesModel::setUpTable(QTableView& table) {
    table.setFrameShape(QFrame::NoFrame);
    table.setSelectionBehavior(QAbstractItemView::SelectRows);
    table.setSelectionMode(QAbstractItemView::SingleSelection);
    table.setEditTriggers(QAbstractItemView::NoEditTriggers);
    table.setShowGrid(false);
    table.setAlternatingRowColors(true);
    table.setWordWrap(false);
    table.verticalHeader()->hide();
    table.verticalHeader()->setDefaultSectionSize(30);
    table.verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table.horizontalHeader()->setHighlightSections(false);
    table.horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table.setTextElideMode(Qt::ElideRight);
    for (const int column : {0, 2, 6}) {
        table.horizontalHeader()->setSectionResizeMode(column, QHeaderView::Stretch);
    }
    for (const int column : {1, 3, 4, 5, 7}) {
        table.horizontalHeader()->setSectionResizeMode(column, QHeaderView::Fixed);
        table.horizontalHeader()->resizeSection(column, column == 5 ? 104 : 72);
    }
    // Sortable by any column, as Finder's lists; unsorted (as stored) to begin with.
    table.horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    table.horizontalHeader()->setSortIndicatorShown(true);
    table.setSortingEnabled(true);
    sortOnClicksThenReset(table.horizontalHeader()); // a third click: as stored again
}

void GamesModel::setDatabase(std::shared_ptr<const gamedb::Database> db) {
    m_db = std::move(db);
    refresh();
}

void GamesModel::setText(const QString& text) {
    const std::string value = text.trimmed().toStdString();
    if (value != m_query.text) {
        m_query.text = value;
        refresh();
    }
}

void GamesModel::setDates(const std::string& from, const std::string& to) {
    if (from != m_query.fromDate || to != m_query.toDate) {
        m_query.fromDate = from;
        m_query.toDate = to;
        refresh();
    }
}

void GamesModel::sort(int column, Qt::SortOrder order) {
    using Order = gamedb::GameQuery::Order;
    static constexpr std::array kOrders{Order::White,    Order::WhiteElo, Order::Black,
                                        Order::BlackElo, Order::Result,   Order::Date,
                                        Order::Event,    Order::Eco};
    const Order wanted = column >= 0 && std::cmp_less(column, kOrders.size())
                             ? kOrders[static_cast<std::size_t>(column)]
                             : Order::Id;
    const bool descending = wanted != Order::Id && order == Qt::DescendingOrder;
    if (wanted != m_query.order || descending != m_query.descending) {
        m_query.order = wanted;
        m_query.descending = descending;
        refresh();
    }
}

void GamesModel::setStatus(const QString& status) {
    if (status != m_status) {
        m_status = status;
        emit statusChanged();
    }
}

void GamesModel::refresh() {
    const int request = ++m_request;
    if (m_cancel) {
        m_cancel->store(true); // the one under way is out of date: stopped, not waited for
    }
    beginResetModel();
    m_pages.clear();
    m_results.clear();
    m_found = m_db && !m_query.plain();
    endResetModel();
    if (!m_found) {
        setStatus({});
        emit loaded();
        return;
    }
    // Only sorting: "Sorting…"; a database that isn't prepared for it yet takes longer.
    const bool onlySorting =
        m_query.text.empty() && m_query.fromDate.empty() && m_query.toDate.empty();
    if (!onlySorting) {
        setStatus(tr("Searching…"));
    } else if (m_db->hasSortIndexes()) {
        setStatus(tr("Sorting…"));
    } else {
        setStatus(tr("Sorting… (faster once this database is prepared for it, in a minute)"));
    }
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    using Found = std::vector<gamedb::GameInfo>;
    // Owned by the model and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher =
        new QFutureWatcher<Found>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<Found>::finished, this, [this, watcher, request] {
        watcher->deleteLater();
        if (request != m_request) {
            return; // asked again meanwhile
        }
        Found found = watcher->result();
        const bool more = found.size() > kLimit;
        if (more) {
            found.resize(kLimit);
        }
        beginResetModel();
        m_results = std::move(found);
        endResetModel();
        setStatus(more ? tr("The first %1 are shown")
                             .arg(QLocale().toString(static_cast<qulonglong>(kLimit)))
                       : QString());
        emit loaded();
    });
    watcher->setFuture(QtConcurrent::run([db = m_db, query = m_query, cancel = m_cancel] {
        return db->find(query, kLimit + 1, cancel.get());
    }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

const gamedb::GameInfo* GamesModel::at(int row) const {
    if (row < 0) {
        return nullptr;
    }
    if (m_found) {
        return static_cast<std::size_t>(row) < m_results.size()
                   ? &m_results[static_cast<std::size_t>(row)]
                   : nullptr;
    }
    if (!m_db) {
        return nullptr;
    }
    const int page = row / kPage;
    auto found = m_pages.find(page);
    if (found == m_pages.end()) {
        if (m_pages.size() > 256) {
            m_pages.clear(); // a few MB at most
        }
        found =
            m_pages
                .emplace(page, m_db->games((static_cast<gamedb::GameId>(page) * kPage) + 1, kPage))
                .first;
    }
    const auto index = static_cast<std::size_t>(row % kPage);
    return index < found->second.size() ? &found->second[index] : nullptr;
}

int GamesModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    if (m_found) {
        return static_cast<int>(m_results.size());
    }
    return m_db ? static_cast<int>(m_db->gameCount()) : 0;
}

int GamesModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 8;
}

QVariant GamesModel::data(const QModelIndex& index, int role) const {
    if (role == Qt::TextAlignmentRole &&
        (index.column() == 1 || index.column() == 3 || index.column() == 4)) {
        return QVariant::fromValue(Qt::AlignCenter);
    }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return {};
    }
    const gamedb::GameInfo* game = at(index.row());
    if (game == nullptr) {
        return {};
    }
    const auto text = [](const std::string& value) {
        return QString::fromStdString(value);
    };
    const auto elo = [](int value) {
        return value > 0 ? QString::number(value) : QString();
    };
    switch (index.column()) {
    case 0:
        return text(game->white);
    case 1:
        return elo(game->whiteElo);
    case 2:
        return text(game->black);
    case 3:
        return elo(game->blackElo);
    case 4:
        return resultText(game->result);
    case 5:
        return text(game->date);
    case 6:
        return text(game->event);
    case 7:
        return text(game->eco);
    default:
        return {};
    }
}

QVariant GamesModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    static const std::array<const char*, 8> kTitles{"WHITE",  "ELO",  "BLACK", "ELO",
                                                    "RESULT", "DATE", "EVENT", "ECO"};
    return tr(kTitles[static_cast<std::size_t>(section)]);
}

} // namespace kiebitz::app
