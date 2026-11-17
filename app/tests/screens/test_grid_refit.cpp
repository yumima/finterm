// tests/screens/test_grid_refit.cpp
//
// A dashboard canvas narrower than 1000 px drops from 12 to 9 columns. The
// old path clamped each tile's x/w and compacted vertically — two tiles side
// by side in 12 columns (x=0 w=5 | x=5 w=7) became x=0 w=5 | x=2 w=7, drawn
// on top of each other, and vertical compaction never separates tiles that
// share rows. refit_to_cols must leave no two tiles overlapping.

#include "screens/dashboard/canvas/GridLayout.h"

#include <QtTest/QtTest>

using namespace finterm::screens;

namespace {
GridItem item(const char* id, int x, int y, int w, int h) {
    GridItem it;
    it.id = id;
    it.instance_id = id;
    it.cell = {x, y, w, h, 1, 1};
    return it;
}
bool any_overlap(const QVector<GridItem>& items) {
    for (int i = 0; i < items.size(); ++i)
        for (int j = i + 1; j < items.size(); ++j)
            if (cells_overlap(items[i].cell, items[j].cell))
                return true;
    return false;
}
} // namespace

class TestGridRefit : public QObject {
    Q_OBJECT
  private slots:
    void side_by_side_tiles_do_not_overlap_in_fewer_columns() {
        // The Private Market default: index + upcoming, three lists, rounds.
        const QVector<GridItem> l{item("c50", 0, 0, 5, 4),     item("up", 5, 0, 7, 4),  item("g", 0, 4, 4, 4),
                                  item("l", 4, 4, 4, 4),       item("v", 8, 4, 4, 4),   item("r", 0, 8, 12, 3)};
        for (int cols : {9, 6}) {
            const auto out = refit_to_cols(l, cols);
            QCOMPARE(out.size(), l.size());
            QVERIFY2(!any_overlap(out), qPrintable(QString("overlap at %1 cols").arg(cols)));
            for (const auto& it : out)
                QVERIFY(it.cell.x >= 0 && it.cell.x + it.cell.w <= cols);
        }
    }
    void a_layout_that_fits_is_left_alone() {
        const QVector<GridItem> l{item("a", 0, 0, 6, 3), item("b", 6, 0, 6, 3)};
        const auto out = refit_to_cols(l, 12);
        QCOMPARE(out[0].cell, l[0].cell);
        QCOMPARE(out[1].cell, l[1].cell);
    }
};

QTEST_MAIN(TestGridRefit)
#include "test_grid_refit.moc"
