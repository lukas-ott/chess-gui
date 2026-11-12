#pragma once

#include "kiebitz/gamedb/database.hpp"

#include <QAbstractTableModel>

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

class QTableView;

namespace kiebitz::app {

/// "1-0", "0-1", "½-½" or "*".
[[nodiscard]] QString resultText(gamedb::GameResult result);

/// A database's games as a table. All of them as stored (millions: read a page at a time as they
/// are scrolled to); or, with a search, a date range or a column to sort by, the first
/// kLimit games that fit, asked for in the background.
class GamesModel : public QAbstractTableModel {
    Q_OBJECT

public:
    static constexpr std::size_t kLimit = 10000;

    explicit GamesModel(QObject* parent = nullptr);

    /// The look every games table shares: the columns' widths, rows, no grid; a click on a
    /// column's title sorts by it (again: the other way round).
    static void setUpTable(QTableView& table);

    /// The games of this database (null: none), with the search, dates and order kept.
    void setDatabase(std::shared_ptr<const gamedb::Database> db);
    void setText(const QString& text);
    void setDates(const std::string& from, const std::string& to);
    [[nodiscard]] const gamedb::GameInfo* at(int row) const;
    /// "Searching…", "The first 10,000 are shown", or empty.
    [[nodiscard]] QString status() const { return m_status; }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role) const override;
    void sort(int column, Qt::SortOrder order) override;

signals:
    void statusChanged();
    /// The games shown are there (after a search, a sort, new dates).
    void loaded();

private:
    void refresh();
    void setStatus(const QString& status);

    static constexpr int kPage = 256;
    std::shared_ptr<const gamedb::Database> m_db;
    gamedb::GameQuery m_query;
    bool m_found = false; ///< showing the answer to m_query, not all games as stored
    std::vector<gamedb::GameInfo> m_results;
    mutable std::unordered_map<int, std::vector<gamedb::GameInfo>> m_pages;
    int m_request = 0;
    std::shared_ptr<std::atomic<bool>> m_cancel; ///< the query under way: set to stop it
    QString m_status;
};

} // namespace kiebitz::app
