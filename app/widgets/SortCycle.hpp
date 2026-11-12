#pragma once

#include <QHeaderView>
#include <QObject>

namespace kiebitz::app {

/// Finder-like sorting by a column's title: one click sorts by it, a second the other way
/// round, a third goes back to how the table started (`section` -1: as stored; or a column and
/// order, e.g. the most games first).
inline void sortOnClicksThenReset(QHeaderView* header, int section = -1,
                                  Qt::SortOrder order = Qt::AscendingOrder) {
    // The clicks in a row on the same title, kept on the header itself.
    QObject::connect(header, &QHeaderView::sectionClicked, header,
                     [header, section, order](int clicked) {
                         const bool same = header->property("sortClickSection").toInt() == clicked;
                         const int clicks = same ? header->property("sortClicks").toInt() + 1 : 1;
                         header->setProperty("sortClickSection", clicked);
                         header->setProperty("sortClicks", clicks);
                         if (clicks >= 3) {
                             header->setProperty("sortClicks", 0);
                             header->setProperty("sortClickSection", -2);
                             header->setSortIndicator(section, order); // the view sorts so
                         }
                     });
    header->setProperty("sortClickSection", -2);
}

} // namespace kiebitz::app
