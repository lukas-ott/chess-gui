#pragma once

#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace kiebitz::app {

/// A row of a sortable QTreeWidget: by a column's number where it has one (kSortRole: games,
/// percentages, ratings), else by its text as people read it ("Nf3" before "d4" ignores case).
class SortableItem : public QTreeWidgetItem {
public:
    static constexpr int kSortRole = Qt::UserRole + 50;
    using QTreeWidgetItem::QTreeWidgetItem;

    void setSortValue(int column, double value) { setData(column, kSortRole, value); }

    [[nodiscard]] bool operator<(const QTreeWidgetItem& other) const override {
        const int column = treeWidget() != nullptr ? treeWidget()->sortColumn() : 0;
        const QVariant mine = data(column, kSortRole);
        const QVariant theirs = other.data(column, kSortRole);
        if (mine.isValid() && theirs.isValid()) {
            return mine.toDouble() < theirs.toDouble();
        }
        return text(column).localeAwareCompare(other.text(column)) < 0;
    }
};

} // namespace kiebitz::app
