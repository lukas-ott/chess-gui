#include "GamesModel.hpp"

#include <QHeaderView>
#include <QTableView>

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
        table.horizontalHeader()->resizeSection(column, column == 5 ? 96 : 64);
    }
}

} // namespace kiebitz::app
