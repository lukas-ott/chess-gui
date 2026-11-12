#pragma once

#include "kiebitz/gamedb/database.hpp"

#include <QAbstractTableModel>

#include <memory>
#include <unordered_map>
#include <vector>

class QTableView;

namespace kiebitz::app {

/// "1-0", "0-1", "½-½" or "*".
[[nodiscard]] QString resultText(gamedb::GameResult result);

/// A database's games as a table. All of them (millions: read a page at a time as they are
/// scrolled to), or the results of a search.
class GamesModel : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;

    /// The look every games table shares: the columns' widths, rows, no grid.
    static void setUpTable(QTableView& table);

    void setDatabase(std::shared_ptr<const gamedb::Database> db) {
        beginResetModel();
        m_db = std::move(db);
        m_searching = false;
        m_results.clear();
        m_pages.clear();
        endResetModel();
    }
    void setResults(std::vector<gamedb::GameInfo> results) {
        beginResetModel();
        m_searching = true;
        m_results = std::move(results);
        endResetModel();
    }
    [[nodiscard]] const gamedb::GameInfo* at(int row) const {
        if (row < 0) {
            return nullptr;
        }
        if (m_searching) {
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
            found = m_pages
                        .emplace(page, m_db->games((static_cast<gamedb::GameId>(page) * kPage) + 1,
                                                   kPage))
                        .first;
        }
        const auto index = static_cast<std::size_t>(row % kPage);
        return index < found->second.size() ? &found->second[index] : nullptr;
    }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override {
        if (parent.isValid()) {
            return 0;
        }
        if (m_searching) {
            return static_cast<int>(m_results.size());
        }
        return m_db ? static_cast<int>(m_db->gameCount()) : 0;
    }
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : 8;
    }
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override {
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
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
            return {};
        }
        static const std::array<const char*, 8> kTitles{"WHITE",  "ELO",  "BLACK", "ELO",
                                                        "RESULT", "DATE", "EVENT", "ECO"};
        return QObject::tr(kTitles[static_cast<std::size_t>(section)]);
    }

private:
    static constexpr int kPage = 256;
    std::shared_ptr<const gamedb::Database> m_db;
    bool m_searching = false;
    std::vector<gamedb::GameInfo> m_results;
    mutable std::unordered_map<int, std::vector<gamedb::GameInfo>> m_pages;
};

} // namespace kiebitz::app
